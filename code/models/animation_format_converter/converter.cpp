#include <algorithm>
#include <iostream>
#include <glm/gtx/transform.hpp>
#include <sstream>
#include <fstream>
#include <vector>
#include <glm/glm.hpp>
#include <string>
#include <stdint.h>
#include <rapidxml.hpp>
#include <glm/gtx/quaternion.hpp>

//#define DEBUG
#define CORRECTION glm::rotate(glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f))

using namespace rapidxml;

using byte = uint8_t;

std::string get_float_array_str(rapidxml::xml_node<> * source)
{
	return source->value();
}

template <uint32_t Dimension> std::vector<glm::vec<Dimension, float, glm::highp>> extract_vertices_from_line(std::string const & raw, uint32_t float_count, bool flip)
{
#define CORRECTION glm::rotate(glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f))

    std::vector<glm::vec<Dimension, float, glm::highp>> vertices;
    vertices.resize(float_count / Dimension);

    std::istringstream stream(raw);
    
    uint32_t counter = 0;
    std::string current_float;
    
    while (std::getline(stream, current_float, ' ') && counter < float_count)
    {
        uint32_t at_array = counter / Dimension;
        vertices[at_array][counter % Dimension] = std::stof(current_float);

        if constexpr (Dimension == 3)
                     {
                         if (flip && (counter + 1) % 3 == 0 && counter != 0)
                         {
                             vertices[(counter + 1) / 3 - 1] = glm::mat3(CORRECTION) * vertices[(counter + 1) / 3 - 1];
                         }
                     }
        ++counter;
    }
    
    return vertices;
}

std::vector<uint32_t> organize_vertices(std::string const & raw_list,
                                        std::vector<glm::vec3> const & normals_raw,
                                        std::vector<glm::vec2> const & uvs_raw,
                                        std::vector<glm::vec3> & normals_result,
                                        std::vector<glm::vec2> & uvs_result,
                                        uint32_t index_count)
{
    std::vector<uint32_t> indices;

    std::istringstream stream(raw_list);

    uint32_t counter = 0;
    std::string current_int;

    uint32_t current_vertex;

    while (std::getline(stream, current_int, ' '))
    {
        /* push index */
        if (counter % 2 == 0)
        {
            current_vertex = std::stoi(current_int);
            indices.push_back(current_vertex);
        }
        /* set normal at appropriate location */
        else if (counter % 2 == 1)
            if (normals_raw.size())
                normals_result[current_vertex] = normals_raw[std::stoi(current_int)];
        /* set uv coordinates at appropriate location */
        /*        else if (counter % 2 == 2)
            if(uvs_raw.size())
            uvs_result[current_vertex] = uvs_raw[std::stoi(current_int)];*/

        ++counter;
    }

    return indices;
}

struct header
{
    uint32_t vertices_offset;       uint32_t vertices_size;
    uint32_t normals_offset;        uint32_t normals_size;
    uint32_t tcoords_offset;        uint32_t tcoords_size;
    uint32_t joint_weights_offset;  uint32_t joint_weights_size;
    uint32_t joint_ids_offset;      uint32_t joint_ids_size;
    uint32_t indices_offset;        uint32_t indices_size;
};

std::vector<std::string> split(std::string const & str, char const splitter)
{
    std::vector<std::string> words;
    std::string current;
    std::istringstream iss(str);
    while (std::getline(iss, current, splitter)) words.push_back(current);

    return words;
}

std::string create_animation_name(std::string const &original_file_path /* .dae */)
{
    std::string result;

    auto splitted = split(original_file_path, '/');
    auto end_string = splitted.back();
    auto pre_suff_splitted = split(end_string, '.');

    auto whole_animation_name = pre_suff_splitted[pre_suff_splitted.size() - 2];

    auto animation_name_split = split(whole_animation_name, '_');

    return animation_name_split.back();
}

std::string create_output_file(std::string const &original_file_path, const std::string &extension)
{
    std::string result;

    auto splitted = split(original_file_path, '/');
    auto end_string = splitted.back();
    auto pre_suff_splitted = split(end_string, '.');

    pre_suff_splitted.back() = extension;

    auto file_name = pre_suff_splitted[0] + '.' + pre_suff_splitted[1];
    splitted.back() = file_name;

    for (uint32_t i = 0; i < splitted.size() - 1; ++i)
    {
        result += splitted[i] + '/';
    }

    return (result + splitted.back());
}

auto organize_buffer(std::vector<glm::vec3> & normals,
                     std::vector<glm::vec3> & vertices,
                     std::vector<glm::vec2> & texture_coords,
                     std::vector<glm::vec3> & weights,
                     std::vector<glm::ivec3> & joint_ids,
                     std::vector<uint32_t> & indices) -> std::pair<char *, uint32_t>
{    
    auto vertex_buffer_size = vertices.size() * sizeof(glm::vec3);
    auto normal_buffer_size = normals.size() * sizeof(glm::vec3);
    auto tcoords_buffer_size = texture_coords.size() * sizeof(glm::vec2);
    auto index_buffer_size = indices.size() * sizeof(uint32_t);
    auto weights_buffer_size = weights.size() * sizeof(glm::vec3);
    auto joint_ids_buffer_size = joint_ids.size() * sizeof(glm::ivec3);

    header head;

    uint32_t buffer_size = sizeof(header)
        + vertex_buffer_size
        + normal_buffer_size
        + tcoords_buffer_size
        + weights_buffer_size
        + joint_ids_buffer_size
        + index_buffer_size;

    char * byte_buffer = new char[ buffer_size ];

    uint32_t offset_counter = sizeof(header);
    
    head.vertices_offset = offset_counter;
    offset_counter += vertex_buffer_size;
    
    head.normals_offset = offset_counter;
    offset_counter += normal_buffer_size;
    
    head.tcoords_offset = offset_counter;
    offset_counter += tcoords_buffer_size;
    
    head.joint_weights_offset = offset_counter;
    offset_counter += weights_buffer_size;
    
    head.joint_ids_offset = offset_counter;
    offset_counter += joint_ids_buffer_size;
    
    head.indices_offset = offset_counter;

    head.vertices_size = vertex_buffer_size;
    head.normals_size = normal_buffer_size;
    head.tcoords_size = tcoords_buffer_size;
    // TODO: Load weights and joint ids
    head.joint_weights_size = weights_buffer_size;
    head.joint_ids_size = joint_ids_buffer_size;
    head.indices_size = index_buffer_size;

    memcpy(byte_buffer, &head, sizeof header);
    memcpy(byte_buffer + head.vertices_offset, vertices.data(), vertex_buffer_size);
    memcpy(byte_buffer + head.normals_offset, normals.data(), normal_buffer_size);
    memcpy(byte_buffer + head.tcoords_offset, texture_coords.data(), tcoords_buffer_size);
    memcpy(byte_buffer + head.joint_weights_offset, weights.data(), weights_buffer_size);
    memcpy(byte_buffer + head.joint_ids_offset, joint_ids.data(), joint_ids_buffer_size);
    memcpy(byte_buffer + head.indices_offset, indices.data(), index_buffer_size);

    std::vector<byte> bytes;
    bytes.resize(buffer_size);
    memcpy_s(bytes.data(), buffer_size, byte_buffer, buffer_size);

    glm::vec3 * vertces = (glm::vec3 *)(byte_buffer + head.vertices_offset);
    glm::vec3 * normls = (glm::vec3 *)(byte_buffer + head.normals_offset);
    glm::vec2 * tcoods = (glm::vec2 *)(byte_buffer + head.tcoords_offset);
    glm::vec3 * weights_ptr = (glm::vec3 *)(byte_buffer + head.joint_weights_offset);
    glm::ivec3 * joint_ids_ptr = (glm::ivec3 *)(byte_buffer + head.joint_ids_offset);
    uint32_t * indics = (uint32_t *)(byte_buffer + head.indices_offset);

    return std::pair(byte_buffer, buffer_size);
}

std::vector<float> get_joint_weights(rapidxml::xml_node<char> *weights_source)
{
    using namespace rapidxml;

    xml_node<> * float_array = weights_source->first_node("float_array");
    std::istringstream stream(float_array->value());

    uint32_t count = std::stoi(float_array->last_attribute()->value());

    std::vector<float> weights;
    weights.reserve(count);

    std::string current_float;

    while (std::getline(stream, current_float, ' '))
    {
        weights.push_back(std::stof(current_float));
    }

    return weights;
}

void load_joint_weights_and_ids(rapidxml::xml_node<char> * vertex_weights,
                                std::vector<glm::vec3> & weights_dest,
                                std::vector<glm::ivec3> & joint_ids_dest,
                                std::vector<float> const & weights_raw)
{
    std::istringstream vcount(vertex_weights->last_node("vcount")->value());
    std::istringstream weights_and_ids(vertex_weights->last_node("v")->value());
    std::string current;

    std::vector<uint32_t> vcount_array;
    while (std::getline(vcount, current, ' '))
        vcount_array.push_back(std::stoi(current));

    std::vector<uint32_t> weights_and_ids_array;
    while (std::getline(weights_and_ids, current, ' '))
        weights_and_ids_array.push_back(std::stoi(current));

    for (uint32_t vcount_index = 0, weights_ids_index = 0; vcount_index < vcount_array.size(); ++vcount_index)
    {
        uint32_t weight_count = vcount_array[vcount_index];

        std::vector<std::pair<uint32_t, float>> current_weight_and_joint;

        for (uint32_t i = weights_ids_index; weights_ids_index < i + weight_count * 2; weights_ids_index += 2)
        {
            uint32_t joint_id = weights_and_ids_array[weights_ids_index];
            uint32_t weight_id = weights_and_ids_array[weights_ids_index + 1];

            current_weight_and_joint.push_back(std::pair(joint_id, weights_raw[weight_id]));
        }

        std::sort(current_weight_and_joint.begin()
                  , current_weight_and_joint.end()
                  , [](std::pair<uint32_t, float> & lhs, std::pair<uint32_t, float> & rhs) -> bool { return lhs.second > rhs.second; });

        glm::ivec3 joint_ids(0);
        glm::vec3 weights(0.0f);
        float total = 0.0f;
        for (uint32_t i = 0; i < 3 && i < current_weight_and_joint.size(); ++i)
        {
            joint_ids[i] = current_weight_and_joint[i].first;
            weights[i] = current_weight_and_joint[i].second;
            total += weights[i];
        }

        /* get total and make elemnts of weights add up to 1 */
        for (uint32_t i = 0; i < 3; ++i)
            weights[i] = std::min(weights[i] / total, 1.0f);
        weights_dest.push_back(weights);
        joint_ids_dest.push_back(joint_ids);
    }
}

void load_mesh_weights_and_joint_ids(std::vector<glm::vec3> &weights, std::vector<glm::ivec3> &joint_ids, xml_document<> *doc)
{
    xml_node<> * library_controllers = doc->last_node("COLLADA")->last_node("library_controllers")->first_node()->first_node();

    xml_node<> * bind_matrix_node = library_controllers->first_node();
    xml_node<> * joint_names_source = bind_matrix_node->next_sibling();
    xml_node<> * bind_poses_source = joint_names_source->next_sibling();
    xml_node<> * weights_source = bind_poses_source->next_sibling();
    xml_node<> * joints_node = weights_source->next_sibling();
    xml_node<> * vertex_weights_node = joints_node->next_sibling();

    auto weights_raw = get_joint_weights(weights_source);

    load_joint_weights_and_ids(vertex_weights_node, weights, joint_ids, weights_raw);
}

void load_mesh(xml_document<> *doc, std::string const &original_path, const std::string &final_path)
{
    xml_node<> *first_node = doc->first_node("COLLADA");

    /* load mesh data of model */
    xml_node<> *mesh = first_node->first_node("library_geometries")->first_node()->first_node();
    char const *mesh_name = mesh->name();
    xml_node<> *src_vertices = mesh->first_node();
    char const *vertices_name = src_vertices->name();
    xml_node<> *src_normals = src_vertices->next_sibling();
    xml_node<> *src_uvs = src_normals->next_sibling();

    std::string vertices_raw = get_float_array_str(src_vertices->first_node());
    std::string normals_raw = get_float_array_str(src_normals->first_node());
    std::string uvs_raw = get_float_array_str(src_uvs->first_node());

    uint32_t vertices_count = std::stoi(src_vertices->first_node()->last_attribute()->value());
    uint32_t normals_count = std::stoi(src_normals->first_node()->last_attribute()->value());
    // TODO: For now hard code no uvs, but in the future make way for a uvs attribute if there is one provided
    uint32_t uvs_count = 0;

    std::vector<glm::vec3> vertices = extract_vertices_from_line<3>(vertices_raw, vertices_count, true);
    std::vector<glm::vec3> normals = extract_vertices_from_line<3>(normals_raw, normals_count, false);
    //    std::vector<glm::vec2> uvs = extract_vertices_from_line<2>(uvs_raw, uvs_count, false);
    std::vector<glm::vec2> uvs;

    /* load indices of model */
    xml_node<> *indices_node = mesh->last_node();
    xml_node<> *arr = indices_node->last_node()->last_node();

    std::vector<glm::vec3> normals_in_order;
    std::vector<glm::vec2> uvs_in_order;

    if (normals.size()) normals_in_order.resize(vertices.size());
    if (uvs.size()) uvs_in_order.resize(vertices.size());


    // LOAD WEIGHTS AND JOINT IDS
    std::vector<glm::vec3> weights;
    std::vector<glm::ivec3> joint_ids;
    load_mesh_weights_and_joint_ids(weights, joint_ids, doc);

    std::vector<uint32_t> indices = organize_vertices(arr->value(), normals, uvs, normals_in_order, uvs_in_order, std::stoi(indices_node->last_attribute()->value()));

    std::string dst_mesh_custom_file = final_path + ".mesh_custom";//create_output_file(original_path, "mesh_custom");
    
    auto buffer = organize_buffer(normals_in_order, vertices, uvs_in_order, weights, joint_ids, indices);
    auto output_path = create_output_file(original_path, "mesh_custom");
    std::ofstream output(output_path, std::ios::binary);
    output.write(buffer.first, buffer.second);
    output.close();
}





#include <unordered_map>


#define MAX_CHILD_JOINTS 4

struct joint_file_t
{
    uint32_t joint_id = 0;
    uint32_t parent_joint_id = 0;
    uint32_t children_joint_count = 0;
    uint32_t children_joint_ids[MAX_CHILD_JOINTS] = {};
    // TODO: To remove if not needed by the game!
    glm::mat4 bind_transform;
    glm::mat4 inverse_bind_transform;
};



// Skeleton file format:
// joint count (4 bytes)
// name array ->    [name1] 0 (1 byte) [name2] 0 (1 byte) [name3] 0 (1 byte)



struct joint
{
private:
    std::vector<joint *> children;
    joint * parent;

    uint32_t id;
    std::string name;

    glm::mat4 local_bind_transform;
    glm::mat4 inverse_bind_transform;

    glm::mat4 animated_transform;
public:
    joint(std::string const & name, uint32_t id);

    auto add_child(joint * child) -> void;
    auto operator[](uint32_t index) -> joint * &;

    auto calculate_inverse_bind(glm::mat4 const & inverse_parent = glm::mat4(1.0f)) -> void
    {
        inverse_bind_transform = inverse_parent * inverse_bind_transform;

        for (auto & child : children)
        {
            child->calculate_inverse_bind(inverse_bind_transform);
        }
    }

    auto calculate_inverses(glm::mat4 const & bind_parent = glm::mat4(1.0f)) -> void
    {
        glm::mat4 bind_transform = bind_parent * local_bind_transform;
        inverse_bind_transform = glm::inverse(bind_transform);
        for (auto child : children)
        {
            child->calculate_inverses(bind_transform);
        }
    }

    auto get_id(void) -> uint32_t &;
    auto get_name(void) -> std::string &;
    auto get_local_bind_transform(void) -> glm::mat4 &;
    auto get_inverse_bind_transform(void) -> glm::mat4 &;
    auto get_parent(void) -> joint * &;
    auto get_animated_transform(void) -> glm::mat4 &;
    auto get_child_count(void) const -> uint32_t;
};




auto load_joint_map(std::unordered_map<std::string, joint *> & joint_map,
                    std::vector<joint *> & joint_index_map,
                    rapidxml::xml_node<char> * src) -> void
{
    std::istringstream name_array(src->first_node()->value());
    std::string current_name;

    uint32_t index = 0;

    while (std::getline(name_array, current_name, ' '))
    {
        joint_index_map.push_back(new joint(current_name, index++));
        joint_map[current_name] = joint_index_map.back();
    }
}



auto load_hierarchy(rapidxml::xml_node<> * current,
                    std::unordered_map<std::string, joint *> & joint_map, joint * parent) -> joint *
{
    using namespace rapidxml;

    const std::string REMOVE = "Armature_";

    std::string raw_current_name = current->first_attribute()->value();
    std::string current_name = raw_current_name.substr(REMOVE.length());
    //    std::string current_name = raw_current_name;
    joint * current_joint = joint_map[current_name];
    current_joint->get_parent() = parent;

    if (parent) parent->add_child(current_joint);

    glm::mat4 bone_space_transform;

    std::istringstream stream(current->first_node()->value());
    std::string current_float;
    uint32_t count = 0;

    while (std::getline(stream, current_float, ' '))
        bone_space_transform[(count / 4) % 4][count++ % 4] = std::stof(current_float);

    if (!parent) current_joint->get_local_bind_transform() = CORRECTION * glm::transpose(bone_space_transform);
    else current_joint->get_local_bind_transform() = glm::transpose(bone_space_transform);

    /* load for children */
    auto * first = current->first_node("node");

    if (first)
    {
        for (xml_node<> * child = first; child
                 ; child = child->next_sibling("node"))
            load_hierarchy(child, joint_map, current_joint);
    }

    if (!parent) return current_joint;
    else return nullptr;
}

std::vector<byte> get_joint_name_array(std::vector<joint *> &joint_map_by_index)
{
    // Name array
    std::vector<byte> names_bytes;
    
    for (auto j : joint_map_by_index)
    {
        std::string joint_name = j->get_name();
        for (auto c : joint_name)
        {
            names_bytes.push_back(c);
        }

        names_bytes.push_back('\0');
    }

    return names_bytes;
}

void organize_skeleton_file(std::unordered_map<std::string, joint *> &joint_map_by_name,
                            std::vector<joint *> &joint_map_by_index,
                            const std::string &final_path)
{
    // Just contains the joint count
    const uint32_t HEADER_SIZE = sizeof(uint32_t);
    const uint32_t JOINT_ARRAY_SIZE = joint_map_by_index.size() * sizeof(joint_file_t);

    std::vector<byte> name_array_bytes = get_joint_name_array(joint_map_by_index);
    
    std::vector<byte> bytes_vec;
    bytes_vec.resize( HEADER_SIZE + name_array_bytes.size() + JOINT_ARRAY_SIZE );

    byte *bytes = bytes_vec.data();

    uint32_t joint_count = joint_map_by_index.size();

    std::vector<joint_file_t> formatted_joints;
    formatted_joints.resize(joint_count);

    for (uint32_t i = 0; i < joint_count; ++i)
    {
        joint_file_t *formatted_joint = &formatted_joints[i];
        joint *unformatted_joint = joint_map_by_index[i];

        formatted_joint->joint_id = unformatted_joint->get_id();
        if (unformatted_joint->get_parent())
        {
            formatted_joint->parent_joint_id = unformatted_joint->get_parent()->get_id();
        }
        formatted_joint->children_joint_count = unformatted_joint->get_child_count();

        for (uint32_t child = 0; child < formatted_joint->children_joint_count; ++child)
        {
            formatted_joint->children_joint_ids[child] = unformatted_joint->operator[](child)->get_id();
        }

        formatted_joint->bind_transform = unformatted_joint->get_local_bind_transform();
        formatted_joint->inverse_bind_transform = unformatted_joint->get_inverse_bind_transform();
    }

    uint32_t offset = 0;
    memcpy(bytes + offset, &joint_count, HEADER_SIZE);
    offset += HEADER_SIZE;
    memcpy(bytes + offset, name_array_bytes.data(), name_array_bytes.size() * sizeof(byte));
    offset += name_array_bytes.size();
    memcpy(bytes + offset, formatted_joints.data(), formatted_joints.size() * sizeof(joint_file_t));

    //    std::string output_path = create_output_file(file_path, "skeleton_custom");
    std::string output_path = final_path;
    
    std::ofstream output(output_path, std::ios::binary);
    output.write((char*)bytes, bytes_vec.size());
    output.close();
}

std::pair<joint *, std::unordered_map<std::string, joint *>> load_skeleton(xml_document<> *doc, const std::string &file_path, const std::string &final_path)
{
    xml_node<> * library_controllers = doc->last_node("COLLADA")->last_node("library_controllers")->first_node()->first_node();

    xml_node<> * bind_matrix_node = library_controllers->first_node();
    xml_node<> * joint_names_source = bind_matrix_node->next_sibling();

    std::unordered_map<std::string, joint *> joint_map;
    std::vector<joint *> index_joint_map;
    load_joint_map(joint_map, index_joint_map, joint_names_source);

    /* load joint hierarchy */
    xml_node<> * library_visual_scenes = doc->last_node("COLLADA")->last_node("library_visual_scenes");
    xml_node<> * visual_scene = library_visual_scenes->first_node();
    xml_node<> * armature = visual_scene->first_node()->next_sibling();
    xml_node<> * head = armature->first_node("node");

    joint * root = load_hierarchy(head, joint_map, nullptr);

    root->calculate_inverses();

    organize_skeleton_file(joint_map, index_joint_map, final_path);

    return {root, std::move(joint_map)};
}







struct joint_transform
{
    glm::quat rotation;
    glm::vec3 position;
};

class key_frame
{
public:
    float time_stamp;

    std::unordered_map<std::string, joint_transform> joint_transforms;
public:
    key_frame(float time_stamp);

    auto get_time_stamp(void) -> float;

    auto operator[](std::string const & name) -> joint_transform &;

    auto begin(void)
    {
        return joint_transforms.begin();
    }

    auto end(void)
    {
        return joint_transforms.end();
    }
};


std::vector<key_frame> get_key_frames(rapidxml::xml_node<char> * animations_node)
{
    std::vector<key_frame> key_frames;

    std::istringstream stream(animations_node->value());
    std::string current_time_stamp;

    while (std::getline(stream, current_time_stamp, ' '))
    {
        key_frames.push_back(key_frame(std::stof(current_time_stamp)));
    }

    return key_frames;
}

void load_key_frame(rapidxml::xml_node<char> * animation, std::vector<key_frame> & frames, joint * root)
{
    for (xml_node<> * a_node = animation; a_node; a_node = a_node->next_sibling())
    {
        /* get to the matrix output */
        xml_node<> * src = a_node->first_node()->next_sibling();

        /* load key frame data */
        /* get name of joint */
        std::string joint_name;
        std::string whole_id = src->first_attribute()->value();
        /* get the joint name */
        std::istringstream stream(whole_id);
        std::string current_word;
        uint32_t count = 0;
        while (std::getline(stream, current_word, '_'))
        {
            if (current_word == "pose")
            {
                joint_name.pop_back();
                break;
            }
            else if (count++ >= 1)
            {
                joint_name += current_word + '_';
            }
        }

        /* get float array */
        xml_node<> * float_array = src->first_node();

        std::istringstream stream_floats(float_array->value());
        std::string current_float;
        uint32_t float_count = 0;
        glm::mat4 current_matrix;
        while (std::getline(stream_floats, current_float, ' '))
        {
            current_matrix[(float_count / 4) % 4][float_count % 4] = std::stof(current_float);

            ++float_count;

            if (float_count % 16 == 0 && float_count != 0)
            {
                if (joint_name == root->get_name())
                {
                    current_matrix = CORRECTION * glm::transpose(current_matrix);
                }
                else current_matrix = glm::transpose(current_matrix);

                /* convert to position and quaternion */
                glm::vec3 position = glm::vec3(current_matrix[3][0], current_matrix[3][1], current_matrix[3][2]);
                glm::quat rotation = glm::quat_cast(current_matrix);

                auto & transform = frames[float_count / 16 - 1][joint_name];
                transform.position = position;
                transform.rotation = rotation;
            }
        }
    }
}

// Animation count
// Key frame name
// float time stamp
// Array of joint transforms (index = joint id) for each key frame
// Key frames are stored in just a big array

// TODO: MAKE SURE THAT THE FILE FORMAT SUPPORTS IMPORTATION OF MULTIPLE ANIMATION CYCLE (e.g. walking, running, idle )

// Animation count | ( Animation name | Animation key frame count | Animation key frame joints per frame | ( Time stamp | ( Joint transforms ) ) )



void organize_animations_buffer(std::vector<std::vector<key_frame>> &animations,
                                std::unordered_map<std::string, joint *> &map,
                                const std::vector<std::string> &file_path,
                                const std::vector<std::string> &animation_names,
                                const std::string &final_path)
{
    const uint32_t ANIMATION_COUNT_SECTION_SIZE = sizeof(uint32_t);
    // Animation name
    const uint32_t ANIMATION_KEY_FRAME_INFO_SIZE = sizeof(uint32_t) * 2;

    uint32_t key_frames_section_size = 0;
    for (uint32_t i = 0; i <  animations.size(); ++i)
    {
        uint32_t animation_frame_count = animations[i].size();
        key_frames_section_size += (sizeof(float) + sizeof(joint_transform) * map.size()) * animation_frame_count;
    }

    uint32_t animation_names_size = 0;
    for (uint32_t i = 0; i < animations.size(); ++i)
    {
        animation_names_size += animation_names[i].length() + 1 /* '\0' */;
    }
    
    uint32_t buffer_size = ANIMATION_COUNT_SECTION_SIZE + ANIMATION_KEY_FRAME_INFO_SIZE * animations.size() + key_frames_section_size + animation_names_size;
    
    std::vector<byte> bytes_vec;
    bytes_vec.resize(buffer_size);
    byte *original = bytes_vec.data();
    byte *current_byte = bytes_vec.data();

    uint32_t animation_count = animations.size();
    memcpy(current_byte, &animation_count, sizeof(uint32_t));

    current_byte += sizeof(uint32_t);
    
    for (uint32_t animation = 0; animation < animations.size(); ++animation)
    {
        std::vector<key_frame> &key_frames = animations[animation];
        //        std::vector<byte> bytes_vec;
        //        bytes_vec.resize( sizeof(uint32_t) * 2 + key_frames.size() * key_frame_formatted_size );

        std::string current_animation_name = animation_names[animation];
        memcpy(current_byte, current_animation_name.c_str(), current_animation_name.length() + 1 /* '\0' */);
        current_byte += current_animation_name.length() + 1;

        
        uint32_t key_frame_count = key_frames.size();
        uint32_t joint_transform_per_key_frame = map.size();


        
        memcpy(current_byte, &key_frame_count, sizeof(uint32_t));
        current_byte += sizeof(uint32_t);
        memcpy(current_byte, &joint_transform_per_key_frame, sizeof(uint32_t));
        current_byte += sizeof(uint32_t);

        
        uint32_t key_frame_formatted_size = sizeof(joint_transform) * map.size();

        for (uint32_t k = 0; k < key_frames.size(); ++k)
        {
            key_frame *current_frame = &key_frames[k];
        
            float *time_stamp_ptr = (float *)current_byte;
            *time_stamp_ptr = current_frame->get_time_stamp();

            current_byte += sizeof(float);

            byte *transform_array_start = current_byte;
            joint_transform *transforms_array = (joint_transform *)transform_array_start;
        
            for (auto j : *current_frame)
            {
                std::string joint_name = j.first;
                uint32_t joint_index = map[joint_name]->get_id();

                joint_transform *transform = &j.second;

                memcpy(transforms_array + joint_index, transform, sizeof(joint_transform));
            }
        
            current_byte += key_frame_formatted_size;
        }
    }

    auto output_path = final_path;
    std::ofstream output(output_path, std::ios::binary);
    output.write((char *)bytes_vec.data(), bytes_vec.size());
    output.close();
}

void load_animations(std::vector<xml_document<> *> &docs,
                     const std::vector<std::string> &file_paths,
                     joint *root,
                     std::unordered_map<std::string, joint *> &map,
                     const std::vector<std::string> &animation_names,
                     const std::string &final_path)
{
    std::vector<std::vector<key_frame>> animations;// = get_key_frames(first_joint_animation_node->first_node()->first_node()/* float array */);
    animations.resize(docs.size());

    for (uint32_t i = 0; i < docs.size(); ++i)
    {
        xml_node<> * library_animations = docs[i]->last_node("COLLADA")->last_node("library_animations");
        xml_node<> * first_joint_animation_node = library_animations->first_node();

        animations[i] = get_key_frames(first_joint_animation_node->first_node()->first_node()/* float array */);

        load_key_frame(library_animations->first_node(), animations[i], root);
    }

    organize_animations_buffer(animations, map, file_paths, animation_names, final_path);
}

#define DEBUG

// Converter.exe ../spaceman ../spaceman_walk.dae ../spaceman_idle.dae ...
int32_t main(int32_t argc, char *argv[])
{
#if defined (DEBUG)
    std::string file_path = "../spaceman.dae";
#endif

#if defined (DEBUG)
    const char *argvs[] = { "Converter.exe\0", "../spaceman\0", "../spaceman_walk.dae\0", "../spaceman_idle.dae\0", "../spaceman_run.dae\0", "../spaceman_hover.dae\0", "../spaceman_sliding.dae\0", "../spaceman_sitting.dae\0" };
    int32_t arg_count = sizeof(argvs) / sizeof(argvs[0]);
#else
    int32_t arg_count = argc;
    char **argvs = argv;
#endif
    
    std::string dst_string = argvs[1];
    std::vector<std::string> paths;
    paths.resize(arg_count - 2);
    for (uint32_t i = 0; i < paths.size(); ++i)
    {
        paths[i] = argvs[i + 2];
    }


    std::ifstream ifile(paths[0]);
    if (!ifile.good())
    {
        assert(0);
        std::cin.get();
        return(0);
    }

    auto dst_animations_file_name_path_split = split(dst_string, '/');
    // TODO: Fix the hardcoded ../
    
    std::string dst_animations_file_name = "../" + dst_animations_file_name_path_split.back() + ".animations_custom";
    
    std::cout << "Starting session" << std::endl;

    std::vector<std::string> contents;
    contents.resize(paths.size());

    std::vector<std::ifstream> files;
    files.resize(paths.size());

    std::vector<std::string> animation_names;
    animation_names.resize(paths.size());

    std::vector<xml_document<> *> dae_docs;
    dae_docs.resize(paths.size());
    for (uint32_t i = 0; i < paths.size(); ++i)
    {
        files[i] = std::ifstream(paths[i]);
        if (!files[i].good())
        {
            assert(0);
            std::cin.get();
            return(0);
        }
        
        contents[i] = std::string(std::istreambuf_iterator<char>(files[i]), std::istreambuf_iterator<char>());
        animation_names[i] = create_animation_name(paths[i]);
        
        dae_docs[i] = new xml_document<>();
        dae_docs[i]->parse<0>(const_cast<char *>(contents[i].c_str()));
    }

    // Use the same file (all files will contain the same bone and mesh information)
    load_mesh(dae_docs[0], paths[0], dst_animations_file_name_path_split.back());
    auto unformatted_root_and_map = load_skeleton(dae_docs[0], paths[0], dst_animations_file_name_path_split.back());

    // TODO: Load animation into a .animation_custom file
    load_animations(dae_docs, paths, unformatted_root_and_map.first, unformatted_root_and_map.second, animation_names, dst_animations_file_name);

    std::cout << "Finished session" << std::endl;
}





















joint::joint(std::string const & name, uint32_t id)
	: id(id), name(name)
{
}

auto joint::add_child(joint * child) -> void
{
	children.push_back(child);
}

auto joint::operator[](uint32_t index) -> joint * &
{
	return children[index];
}

auto joint::get_id(void) -> uint32_t &
{
	return id;
}

auto joint::get_name(void) -> std::string &
{
	return name;
}

auto joint::get_local_bind_transform(void) -> glm::mat4 &
{
	return local_bind_transform;
}

auto joint::get_parent(void) ->  joint * &
{
	return parent;
}

auto joint::get_inverse_bind_transform(void) -> glm::mat4 &
{
	return inverse_bind_transform;
}

auto joint::get_animated_transform(void) -> glm::mat4 &
{
	return animated_transform;
}

auto joint::get_child_count(void) const -> uint32_t
{
	return children.size();
}


key_frame::key_frame(float time_stamp)
	: time_stamp(time_stamp)
{
}

auto key_frame::get_time_stamp(void) -> float
{
	return time_stamp;
}

auto key_frame::operator[](std::string const & name) -> joint_transform &
{
	return joint_transforms[name];
}
