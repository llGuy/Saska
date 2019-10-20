#include <iostream>
#include <array>
#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include <future>
#include <fstream>
#include <stdio.h>
#include <sstream>
#include <stdint.h>
#include <glm/glm.hpp>
#include <glm/gtx/string_cast.hpp>

//#define DEBUG

using byte = uint8_t;

auto split(std::string const & str, char const splitter) -> std::vector<std::string>
{
    std::vector<std::string> words;
    std::string current;
    std::istringstream iss(str);
    while (std::getline(iss, current, splitter)) words.push_back(current);

    return words;
}

auto process_vertex(std::vector<std::string> const & vertex_data,
                    std::vector<uint32_t> & indices, std::vector<glm::vec2> const & raw_textures,
                    std::vector<glm::vec3> const & raw_normals,
                    std::vector<glm::vec2> & textures, std::vector<glm::vec3> & normals) -> void
{
    int32_t current_vertex = std::stoi(vertex_data[0]) - 1;
    indices.push_back(current_vertex);

    if (raw_textures.size() > 0)
    {
        glm::vec2 current_tex = raw_textures[std::stoi(vertex_data[1]) - 1];
        textures[current_vertex] = current_tex;
    }

    if (raw_normals.size() > 0)
    {
        glm::vec3 current_normal = raw_normals[std::stoi(vertex_data[2]) - 1];
        normals[current_vertex] = current_normal;
    }
}

auto break_face_line(std::vector<std::string> const & face_line_words,
                     std::vector<uint32_t> & indices, std::vector<glm::vec2> const & raw_textures,
                     std::vector<glm::vec3> const & raw_normals,
                     std::vector<glm::vec2> & textures, std::vector<glm::vec3> & normals) -> void
{
    std::array<std::vector<std::string>, 3> face_indices;
    for (uint32_t i = 0; i < 3; ++i)
    {
        face_indices[i] = split(face_line_words[i + 1], '/');
        process_vertex(face_indices[i], indices, raw_textures, raw_normals, textures, normals);
    }
}

auto load_model_data(std::ifstream & file
                     , std::vector<glm::vec3> & normals
                     , std::vector<glm::vec3> & vertices
                     , std::vector<glm::vec2> & texture_coords
                     , std::vector<uint32_t> & indices) -> void
{
    /* won't be in the  correct order */
    std::vector<glm::vec2> raw_texture_coords;
    std::vector<glm::vec3> raw_normals;

    std::string line;
    while (std::getline(file, line))
    {
        std::vector<std::string> words = split(line, ' ');

        if (words[0] == "v")
        {
            glm::vec3 vertex;
            for (uint32_t i = 0; i < 3; ++i) vertex[i] = std::stof(words[i + 1]);
            vertices.push_back(vertex);
        }
        else if (words[0] == "vt")
        {
            glm::vec2 texture_coord;
            for (uint32_t i = 0; i < 2; ++i) texture_coord[i] = std::stof(words[i + 1]);
            raw_texture_coords.push_back(glm::vec2(texture_coord.x, 1.0f - texture_coord.y));
        }
        else if (words[0] == "vn")
        {
            glm::vec3 normal;
            for (uint32_t i = 0; i < 3; ++i) normal[i] = std::stof(words[i + 1]);
            raw_normals.push_back(normal);
        }
        else if (words[0] == "f")
        {
            if (raw_normals.size())
            {
                normals.resize(vertices.size());
            }
            if (texture_coords.size())
            {
                texture_coords.resize(vertices.size());
            }

            break_face_line(words, indices, raw_texture_coords, raw_normals, texture_coords, normals);

            break;
        }
    }

    while (std::getline(file, line))
    {
        std::vector<std::string> words = split(line, ' ');
        if (words[0] == "f")
        {
            break_face_line(words, indices, raw_texture_coords, raw_normals, texture_coords, normals);
        }
    }
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

/* .mesh_custom is organized like this
 * - header (4 uints)
 * - vertices
 * - normals
 * - tcoords
 * - indices */
auto organize_buffer(std::vector<glm::vec3> & normals
                     , std::vector<glm::vec3> & vertices
                     , std::vector<glm::vec2> & texture_coords
                     , std::vector<uint32_t> & indices) -> std::pair<char *, uint32_t>
{
    auto vertex_buffer_size = vertices.size() * sizeof(glm::vec3);
    auto normal_buffer_size = normals.size() * sizeof(glm::vec3);
    auto tcoords_buffer_size = texture_coords.size() * sizeof(glm::vec2);
    auto index_buffer_size = indices.size() * sizeof(uint32_t);
    auto weights_buffer_size = 0;
    auto joint_ids_buffer_size = 0;

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
    head.joint_weights_size = 0;
    head.joint_ids_size = 0;
    head.indices_size = index_buffer_size;

    memcpy(byte_buffer, &head, sizeof header);
    memcpy(byte_buffer + head.vertices_offset, vertices.data(), vertex_buffer_size);
    memcpy(byte_buffer + head.normals_offset, normals.data(), normal_buffer_size);
    memcpy(byte_buffer + head.tcoords_offset, texture_coords.data(), tcoords_buffer_size);
    //TODO: Load weights and joint ids
    //memcpy(byte_buffer + head.joint_weights_offset, /*pointer to weights / joint ids*/, weights_buffer_size);
    //memcpy(byte_buffer + head.joint_weights_offset, /*pointer to weights / joint ids*/, joint_ids_buffer_size);
    memcpy(byte_buffer + head.indices_offset, indices.data(), index_buffer_size);

    std::vector<byte> bytes;
    bytes.resize(buffer_size);
    memcpy_s(bytes.data(), buffer_size, byte_buffer, buffer_size);

    glm::vec3 * vertces = (glm::vec3 *)(byte_buffer + head.vertices_offset);
    glm::vec3 * normls = (glm::vec3 *)(byte_buffer + head.normals_offset);
    glm::vec2 * tcoods = (glm::vec2 *)(byte_buffer + head.tcoords_offset);
    uint32_t * indics = (uint32_t *)(byte_buffer + head.indices_offset);

    return std::pair(byte_buffer, buffer_size);
}

auto create_output_file(std::string const & original_file_path) -> std::string
{
    std::string result;

    auto splitted = split(original_file_path, '/');
    auto end_string = splitted.back();
    auto pre_suff_splitted = split(end_string, '.');

    pre_suff_splitted.back() = "mesh_custom";

    auto file_name = pre_suff_splitted[0] + '.' + pre_suff_splitted[1];
    splitted.back() = file_name;

    for (uint32_t i = 0; i < splitted.size() - 1; ++i)
    {
        result += splitted[i] + '/';
    }

    return (result + splitted.back());
}

template <typename T> auto copy_from_pointer(T * ptr, std::size_t size) -> std::vector<T>
{
    std::vector<T> arr;
    arr.reserve(size / sizeof(T));

    for (std::size_t i = 0; i < size / sizeof(T); ++i)
        arr.push_back(ptr[i]);

    return arr;
}

auto load_from_mesh_custom(std::string const & file_path) -> void
{
    std::ifstream file(file_path, std::ios::binary | std::ios::ate);

    std::size_t file_size_bytes = file.tellg();

    std::vector<char> byte_buffer;
    byte_buffer.resize(file_size_bytes);

    file.clear();
    file.seekg(0);
    file.read(byte_buffer.data(), file_size_bytes);

    header head = *(header *)(byte_buffer.data());

    auto * p_vertices = (glm::vec3 *)(byte_buffer.data() + head.vertices_offset);
    auto * p_normals = (glm::vec3 *)(byte_buffer.data() + head.normals_offset);
    auto * p_tcoords = (glm::vec2 *)(byte_buffer.data() + head.tcoords_offset);
    auto * p_indices = (uint32_t *)(byte_buffer.data() + head.indices_offset);

    auto f_vertices = std::async(std::launch::async, copy_from_pointer<glm::vec3>, p_vertices, head.vertices_size);
    auto f_normals = std::async(std::launch::async, copy_from_pointer<glm::vec3>, p_normals, head.normals_size);
    auto f_tcoords = std::async(std::launch::async, copy_from_pointer<glm::vec2>, p_tcoords, head.tcoords_size);
    auto f_indices = std::async(std::launch::async, copy_from_pointer<uint32_t>, p_indices, head.indices_size);

    std::vector<glm::vec3> vertices = std::move(f_vertices.get());
    std::vector<glm::vec3> normals = std::move(f_normals.get());
    std::vector<glm::vec2> tcoords = std::move(f_tcoords.get());
    std::vector<uint32_t> indices = std::move(f_indices.get());
}

auto main(int32_t argc, char * argv[]) -> int32_t
{
#if defined(DEBUG)
    std::string file_path("../debug_mesh.obj");
#else
    /* obj file to load from */
    std::string file_path = argv[1];
#endif

    std::cout << "Converting " << file_path << std::endl;

    std::ifstream ifile(file_path);

    if (!ifile.good())
    {
        assert(0);
        std::cin.get();
        return 0;
    }

    std::vector<glm::vec3> vertices;
    std::vector<glm::vec3> normals;
    std::vector<glm::vec2> texture_coords;
    std::vector<uint32_t> indices;
	
    load_model_data(ifile, normals, vertices, texture_coords, indices);

    auto buffer = organize_buffer(normals, vertices, texture_coords, indices);

    auto output_path = create_output_file(file_path);
	
    std::ofstream output(output_path, std::ios::binary);

    output.write(buffer.first, buffer.second);
	
    output.close();

    std::cout << "Done" << std::endl;
    std::cin.get();
}
