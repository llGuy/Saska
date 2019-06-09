#include <array>
#include <vector>
#include <string>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include "load.hpp"

void
process_vertex(std::vector<std::string> const & vertex_data,
	       std::vector<u32> & indices, std::vector<glm::vec2> const & raw_textures,
	       std::vector<glm::vec3> const & raw_normals,
	       std::vector<glm::vec2> & textures, std::vector<glm::vec3> & normals
	       , u32 & triangle_vertex)
{
    s32 current_vertex = std::stoi(vertex_data[0]) - 1;
    indices.push_back(current_vertex);

    if (raw_textures.size() > 0)
    {
	glm::vec2 current_tex = raw_textures[std::stoi(vertex_data[1]) - 1];
	textures[current_vertex] = current_tex;
    }

    if (raw_normals.size() > 0)
    {
	glm::vec3 current_normal = raw_normals[std::stoi(vertex_data[2]) - 1];
	f32 normal_y = current_normal.y;
	f32 normal_z = current_normal.z;
	
	normals[current_vertex] = current_normal;
    }

    triangle_vertex = current_vertex;
}

std::vector<std::string>
split(std::string const & str, char const splitter)
{
    std::vector<std::string> words;
    std::string current;
    std::istringstream iss(str);
    while (std::getline(iss, current, splitter)) words.push_back(current);

    return words;
}
    
void
break_face_line(const std::vector<std::string> &face_line_words,
		std::vector<u32> &indices, const std::vector<glm::vec2> &raw_textures,
		std::vector<glm::vec3> const &raw_normals,
		std::vector<glm::vec2> &textures, std::vector<glm::vec3> &normals,
		std::vector<glm::vec3> &tangents, std::vector<f32> &tangent_amounts,
		std::vector<glm::vec3> &vertices)
{
    std::array<std::vector<std::string>, 3> face_indices;

    std::array<u32, 3> triangle_vertices;

    for (u32 i = 0; i < 3; ++i)
    {
	face_indices[i] = split(face_line_words[i + 1], '/');
	process_vertex(face_indices[i], indices, raw_textures, raw_normals, textures, normals, triangle_vertices[i]);
    }

    glm::vec3 delta_pos1 = vertices[triangle_vertices[1]] - vertices[triangle_vertices[0]];
    glm::vec3 delta_pos2 = vertices[triangle_vertices[2]] - vertices[triangle_vertices[0]];

    glm::vec2 delta_uv1 = textures[triangle_vertices[1]] - textures[triangle_vertices[0]];
    glm::vec2 delta_uv2 = textures[triangle_vertices[2]] - textures[triangle_vertices[0]];

    f32 r = 1.0f / (delta_uv1.x * delta_uv2.y - delta_uv1.y * delta_uv2.x);
    delta_pos1 *= delta_uv2.y;
    delta_pos2 *= delta_uv1.y;
    glm::vec3 tangent = delta_pos1 - delta_pos2;
    tangent *= r;

    tangents[triangle_vertices[0]] = tangent;
    tangents[triangle_vertices[1]] = tangent;
    tangents[triangle_vertices[2]] = tangent;

    ++tangent_amounts[triangle_vertices[0]];
    ++tangent_amounts[triangle_vertices[1]];
    ++tangent_amounts[triangle_vertices[2]];
}

void
create_model(std::vector<glm::vec3> &vertices
	     , std::vector<glm::vec3> &normals
	     , std::vector<glm::vec2> &texture_coords
	     , std::vector<u32> &indices
	     , std::vector<glm::vec3> &tangents
	     , Vulkan::Model *object
	     , const std::string &name
	     , Vulkan::GPU *gpu)
{
    enum :u32 {POSITION, NORMAL, UVS, TANGENT};
    
    persist constexpr u32 BINDING_AND_ATTRIBUTE_COUNT = 3;
    
    object->attribute_count = BINDING_AND_ATTRIBUTE_COUNT;
    object->attributes_buffer = (VkVertexInputAttributeDescription *)allocate_free_list(sizeof(VkVertexInputAttributeDescription) * object->attribute_count
											, Alignment(1));

    object->binding_count = BINDING_AND_ATTRIBUTE_COUNT;
    object->bindings = (Vulkan::Model_Binding *)allocate_free_list(sizeof(Vulkan::Model_Binding) * object->binding_count
								       , Alignment(1));

    Vulkan::Model_Binding *bindings = object->bindings;
    bindings[POSITION].begin_attributes_creation(object->attributes_buffer);
    bindings[POSITION].push_attribute(POSITION, VK_FORMAT_R32G32B32_SFLOAT, sizeof(glm::vec3));
    bindings[POSITION].end_attributes_creation();

    bindings[NORMAL].begin_attributes_creation(object->attributes_buffer);
    bindings[NORMAL].push_attribute(NORMAL, VK_FORMAT_R32G32B32_SFLOAT, sizeof(glm::vec3));
    bindings[NORMAL].end_attributes_creation();

    bindings[UVS].begin_attributes_creation(object->attributes_buffer);
    bindings[UVS].push_attribute(UVS, VK_FORMAT_R32G32_SFLOAT, sizeof(glm::vec2));
    bindings[UVS].end_attributes_creation();
    /*
    bindings[TANGENT].begin_attributes_creation(object->attributes_buffer);
    bindings[TANGENT].push_attribute(TANGENT, VK_FORMAT_R32G32B32_SFLOAT, sizeof(glm::vec3));
    bindings[TANGENT].end_attributes_creation();*/

    std::string buffer_name = "buffer." + name + ".buffers";

    R_Mem<Vulkan::Buffer> buffers = register_memory(init_const_str(buffer_name.c_str(), buffer_name.length())
								    , sizeof(Vulkan::Buffer) * BINDING_AND_ATTRIBUTE_COUNT);

    R_Mem<VkCommandPool> command_pool = get_memory("command_pool.graphics_command_pool"_hash);
	
    Vulkan::invoke_staging_buffer_for_device_local_buffer(Memory_Byte_Buffer{sizeof(glm::vec3) * (u32)vertices.size(), vertices.data()}
							  , VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
							  , command_pool.p
							  , &buffers.p[POSITION]
							  , gpu);
    bindings[POSITION].buffer = buffers.p[POSITION].buffer;
    
    Vulkan::invoke_staging_buffer_for_device_local_buffer(Memory_Byte_Buffer{sizeof(glm::vec3) * (u32)normals.size(), normals.data()}
							  , VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
								  , command_pool.p
								  , &buffers.p[NORMAL]
								  , gpu);
    bindings[NORMAL].buffer = buffers.p[NORMAL].buffer;
    
    Vulkan::invoke_staging_buffer_for_device_local_buffer(Memory_Byte_Buffer{sizeof(glm::vec2) * (u32)texture_coords.size(), texture_coords.data()}
							  , VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
								  , command_pool.p
								  , &buffers.p[UVS]
								  , gpu);
    bindings[UVS].buffer = buffers.p[UVS].buffer;
    
    /*	Vulkan::invoke_staging_buffer_for_device_local_buffer(Memory_Byte_Buffer{sizeof(glm::vec3) * (u32)tangents.size(), tangents.data()}
	, command_pool.p
	, &buffers.p[TANGENT]
	, gpu);
	bindings[TANGENT].buffer = buffers.p[TANGENT].buffer;*/
    
    object->index_data.index_type = VK_INDEX_TYPE_UINT32;
    object->index_data.index_offset = 0;
    object->index_data.index_count = indices.size();
    
    std::string ibo_name = "buffer." + name + ".ibo";
    R_Mem<Vulkan::Buffer> ibo = register_memory(init_const_str(ibo_name.c_str(), ibo_name.length())
						    , sizeof(Vulkan::Buffer));
    
    Vulkan::invoke_staging_buffer_for_device_local_buffer(Memory_Byte_Buffer{(u32)indices.size() * sizeof(u32), indices.data()}
							  , VK_BUFFER_USAGE_INDEX_BUFFER_BIT
								  , command_pool.p
								  , ibo.p
								  , gpu);
    
    object->index_data.index_buffer = ibo.p->buffer;
    
    object->create_vbo_list();
}
    
void
load_model_from_obj(const char *filename
		    , Vulkan::Model *dst
		    , const char *model_name
		    , Vulkan::GPU *gpu)
{
    std::ifstream file(filename);

    std::vector<glm::vec3> normals;
    std::vector<glm::vec3> vertices;
    std::vector<glm::vec2> texture_coords;
    std::vector<glm::vec3> tangents;
    std::vector<f32> tangent_amounts;

    /* won't be in the  correct order */
    std::vector<glm::vec2> raw_texture_coords;
    std::vector<glm::vec3> raw_normals;
    std::vector<u32> indices;

    std::string line;
    while (std::getline(file, line))
    {
	std::vector<std::string> words = split(line, ' ');

	if (words[0] == "v")
	{
	    glm::vec3 vertex;
	    for (u32 i = 0; i < 3; ++i) vertex[i] = std::stof(words[i + 1]);
	    vertices.push_back(vertex);
	}
	else if (words[0] == "vt")
	{
	    glm::vec2 texture_coord;
	    for (u32 i = 0; i < 2; ++i) texture_coord[i] = std::stof(words[i + 1]);
	    raw_texture_coords.push_back(glm::vec2(texture_coord.x, 1.0f - texture_coord.y));
	}
	else if (words[0] == "vn")
	{
	    glm::vec3 normal;
	    for (u32 i = 0; i < 3; ++i) normal[i] = std::stof(words[i + 1]);
	    raw_normals.push_back(normal);
	}
	else if (words[0] == "f")
	{
	    tangents.resize(vertices.size());
	    tangent_amounts.resize(vertices.size());
	    normals.resize(vertices.size());
	    texture_coords.resize(vertices.size());

	    break_face_line(words
			    , indices
			    , raw_texture_coords
			    , raw_normals
			    , texture_coords
			    , normals
			    , tangents
			    , tangent_amounts
			    , vertices);

	    break;
	}
    }

    while (std::getline(file, line))
    {
	std::vector<std::string> words = split(line, ' ');
	if (words[0] == "f")
	{
	    break_face_line(words
			    , indices
			    , raw_texture_coords
			    , raw_normals
			    , texture_coords
			    , normals
			    , tangents
			    , tangent_amounts
			    , vertices);
	}
    }

    for (u32 i = 0; i < tangents.size(); ++i)
    {
	tangents[i] /= tangent_amounts[i];
    }

    create_model(vertices
		 , normals
		 , texture_coords
		 , indices
		 , tangents
		 , dst
		 , model_name
		 , gpu);
}

internal u32
get_terrain_index(u32 x, u32 z, u32 depth_z)
{
    return(x + z * depth_z);
}

void
load_3D_terrain_mesh(u32 width_x
		     , u32 depth_z
		     , f32 random_displacement_factor
		     , Vulkan::Model *terrain_mesh_base_model_info
		     , Vulkan::Buffer *mesh_buffer_vbo
		     , Vulkan::Buffer *mesh_buffer_ibo
		     , Vulkan::GPU *gpu)
{
    assert(width_x & 0X1 && depth_z & 0X1);
    
    f32 *vtx = (f32 *)allocate_stack(sizeof(f32) * 2 * width_x * depth_z);
    u32 *idx = (u32 *)allocate_stack(sizeof(u32) * 10 * (((width_x - 1) * (depth_z - 1)) / 2));
    
    for (u32 z = 0; z < depth_z; ++z)
    {
	for (u32 x = 0; x < width_x; ++x)
	{
	    // TODO : apply displacement factor to make terrain less perfect
	    u32 index = (x + depth_z * z) * 2;
	    vtx[index] = (f32)x;
	    vtx[index + 1] = (f32)z;
	}	
    }

    u32 crnt_idx = 0;
    
    for (u32 z = 1; z < depth_z - 1; z += 2)
    {
        for (u32 x = 1; x < width_x - 1; x += 2)
	{
	    idx[crnt_idx++] = get_terrain_index(x, z, depth_z);
	    idx[crnt_idx++] = get_terrain_index(x - 1, z - 1, depth_z);
	    idx[crnt_idx++] = get_terrain_index(x - 1, z, depth_z);
	    idx[crnt_idx++] = get_terrain_index(x - 1, z + 1, depth_z);
	    idx[crnt_idx++] = get_terrain_index(x, z + 1, depth_z);
	    idx[crnt_idx++] = get_terrain_index(x + 1, z + 1, depth_z);
	    idx[crnt_idx++] = get_terrain_index(x + 1, z, depth_z);
	    idx[crnt_idx++] = get_terrain_index(x + 1, z - 1, depth_z);
	    idx[crnt_idx++] = get_terrain_index(x, z - 1, depth_z);
	    idx[crnt_idx++] = 0xFFFFFFFF;
	}
    }
    
    // load data into buffers
    R_Mem<VkCommandPool> command_pool = get_memory("command_pool.graphics_command_pool"_hash);
    Vulkan::invoke_staging_buffer_for_device_local_buffer(Memory_Byte_Buffer{sizeof(f32) * 2 * width_x * depth_z, vtx}
							      , VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
							      , command_pool.p
							      , mesh_buffer_vbo
							      , gpu);
    Vulkan::invoke_staging_buffer_for_device_local_buffer(Memory_Byte_Buffer{sizeof(u32) * 8 * (((width_x - 1) * (depth_z - 1)) / 2), vtx}
							      , VK_BUFFER_USAGE_INDEX_BUFFER_BIT
							      , command_pool.p
							      , mesh_buffer_ibo
							      , gpu);

    terrain_mesh_base_model_info->attribute_count = 3;
    terrain_mesh_base_model_info->attributes_buffer = (VkVertexInputAttributeDescription *)allocate_free_list(sizeof(VkVertexInputAttributeDescription) * terrain_mesh_base_model_info->attribute_count);
    terrain_mesh_base_model_info->binding_count = 2;
    terrain_mesh_base_model_info->bindings = (Vulkan::Model_Binding *)allocate_free_list(sizeof(Vulkan::Model_Binding) * terrain_mesh_base_model_info->binding_count);
    enum :u32 {GROUND_BASE_XY_VALUES_BND = 0, HEIGHT_BND = 1, GROUND_BASE_XY_VALUES_ATT = 0, HEIGHT_ATT = 1};
    // buffer that holds only the x-z values of each vertex - the reason is so that we can create multiple terrain meshes without copying the x-z values each time
    terrain_mesh_base_model_info->bindings[GROUND_BASE_XY_VALUES_BND].begin_attributes_creation(terrain_mesh_base_model_info->attributes_buffer);
    terrain_mesh_base_model_info->bindings[GROUND_BASE_XY_VALUES_BND].push_attribute(GROUND_BASE_XY_VALUES_ATT, VK_FORMAT_R32G32_SFLOAT, sizeof(f32) * 2);
    terrain_mesh_base_model_info->bindings[GROUND_BASE_XY_VALUES_BND].end_attributes_creation();
    // buffer contains the y-values of each mesh and the colors of each mesh
    terrain_mesh_base_model_info->bindings[HEIGHT_BND].begin_attributes_creation(terrain_mesh_base_model_info->attributes_buffer);
    terrain_mesh_base_model_info->bindings[HEIGHT_BND].push_attribute(HEIGHT_ATT, VK_FORMAT_R32_SFLOAT, sizeof(f32));
    terrain_mesh_base_model_info->bindings[HEIGHT_BND].end_attributes_creation();
    
    pop_stack();
    pop_stack();
}

Terrain_Mesh_Instance
load_3D_terrain_mesh_instance(u32 width_x
			      , u32 depth_z
			      , Vulkan::Model *prototype
			      , Vulkan::Buffer *ys_buffer
			      , Vulkan::GPU *gpu)
{
    Terrain_Mesh_Instance ret = {};
    ret.model = prototype->copy();
    ret.ys = (f32 *)allocate_free_list(sizeof(f32) * width_x * depth_z);
    memset(ret.ys, 0, sizeof(f32) * width_x * depth_z);
    
    R_Mem<VkCommandPool> command_pool = get_memory("command_pool.graphics_command_pool"_hash);
    
    Vulkan::invoke_staging_buffer_for_device_local_buffer(Memory_Byte_Buffer{sizeof(f32) * width_x * depth_z, ret.ys}
							      , VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
							      , command_pool.p
							      , ys_buffer
							      , gpu);
    ret.ys_gpu = *ys_buffer;
    enum :u32 {GROUND_BASE_XY_VALUES_BND = 0, HEIGHT_BND = 1, GROUND_BASE_XY_VALUES_ATT = 0, HEIGHT_ATT = 1};
    ret.model.bindings[HEIGHT_BND].buffer = ret.ys_gpu.buffer;

    ret.model.create_vbo_list();
    return(ret);
}

// later on will use proprietary binary file format
void
load_pipelines_from_json(Vulkan::GPU *gpu
			 , Vulkan::Swapchain *swapchain)
{
    persist const char *json_file_name = "config/pipelines.json";

    std::ifstream is (json_file_name);
    is.seekg(0, is.end);
    u32 length = is.tellg();
    is.seekg(0, is.beg);

    char *buffer = new char [sizeof(char) * length];
    memset(buffer, 0, length * sizeof(char));
    is.read(buffer, length);

    std::string content = std::string(buffer, length);
    
    nlohmann::json j = nlohmann::json::parse(content);
    for (nlohmann::json::iterator i = j.begin(); i != j.end(); ++i)
    {
	// get name
	std::string key = i.key();

	bool make_new = i.value().find("new").value();
	
	R_Mem<Vulkan::Graphics_Pipeline> new_ppln;

	if (make_new)
	{
	    new_ppln = register_memory(init_const_str(key.c_str(), key.length())
				       , sizeof(Vulkan::Graphics_Pipeline));
	}
	else
	{
	    new_ppln = get_memory(init_const_str(key.c_str(), key.length()));
	}

	auto stages = i.value().find("stages");
	u32 stg_count = 0;
	persist constexpr u32 MAX_STAGES_COUNT = 5;
	persist VkShaderModule module_buffer[MAX_STAGES_COUNT] = {};
	persist VkPipelineShaderStageCreateInfo shader_infos[MAX_STAGES_COUNT] = {};
	memset(module_buffer, 0, sizeof(module_buffer));
	memset(shader_infos, 0, sizeof(shader_infos));
	for (nlohmann::json::iterator stg = stages.value().begin(); stg != stages.value().end(); ++stg)
	{
	    std::string k = stg.key();
	    VkShaderStageFlagBits vk_stage_flags = {};
	    switch(k[0])
	    {
	    case 'v': {vk_stage_flags = VK_SHADER_STAGE_VERTEX_BIT; break;}
	    case 'f': {vk_stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT; break;}
	    case 'g': {vk_stage_flags = VK_SHADER_STAGE_GEOMETRY_BIT; break;}
	    case 't': {break;}
	    };
	    File_Contents bytecode = read_file(std::string(stg.value()).c_str());
	    Vulkan::init_shader(vk_stage_flags, bytecode.size, bytecode.content, gpu, &module_buffer[stg_count]);
	    Vulkan::init_shader_pipeline_info(&module_buffer[stg_count], vk_stage_flags, &shader_infos[stg_count]);
	    ++stg_count;
	}
	VkPipelineInputAssemblyStateCreateInfo assembly_info = {};
	auto assemble = i.value().find("assemble");
	VkPrimitiveTopology top;
	std::string top_str = assemble.value().find("topology").value();
	switch(top_str[0])
	{
	case 'q': {top = VK_PRIMITIVE_TOPOLOGY_LINE_STRIP; break;}
	case 'c': {top = VK_PRIMITIVE_TOPOLOGY_LINE_LIST; break;}
	case 'f': {top = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN; break;}
	case 'l': {top = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST; break;}
	case 's': {top = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP; break;}
	case 'p': {top = VK_PRIMITIVE_TOPOLOGY_POINT_LIST; break;}
	}
	Vulkan::init_pipeline_input_assembly_info(0, top, bool(assemble.value().find("restart").value()), &assembly_info);

	VkPipelineRasterizationStateCreateInfo rasterization_info = {};
	VkPolygonMode p_mode;
	VkCullModeFlagBits c_mode;
	auto raster_info = i.value().find("raster");
	std::string p_mode_str = raster_info.value().find("poly_mode").value();
	std::string c_mode_str = raster_info.value().find("cull").value();
	switch(p_mode_str[0])
	{
	case 'f': {p_mode = VK_POLYGON_MODE_FILL; break;};
	case 'l': {p_mode = VK_POLYGON_MODE_LINE; break;};
	}
	switch(c_mode_str[0])
	{
	case 'n': {c_mode = VK_CULL_MODE_NONE; break;}
	case 'b': {c_mode = VK_CULL_MODE_BACK_BIT; break;}
	}
	Vulkan::init_pipeline_rasterization_info(p_mode, c_mode, 2.0f, 0, &rasterization_info);

	auto blend_stuff = i.value().find("blend");
	std::vector<std::string> blend_values = blend_stuff.value();
	std::vector<VkPipelineColorBlendAttachmentState> states;
	states.resize(blend_values.size());
	for (u32 b = 0; b < states.size(); ++b)
	{
	    if (blend_values[b] == "disable")
	    {
		Vulkan::init_blend_state_attachment(VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
							, VK_FALSE
							, VK_BLEND_FACTOR_ONE
							, VK_BLEND_FACTOR_ZERO
							, VK_BLEND_OP_ADD
							, VK_BLEND_FACTOR_ONE
							, VK_BLEND_FACTOR_ZERO
							, VK_BLEND_OP_ADD
							, &states[b]);
	    }
	    // other types of blend...
	}
	VkPipelineColorBlendStateCreateInfo blending_info = {};
	Memory_Buffer_View<VkPipelineColorBlendAttachmentState> mbf_states {(u32)states.size(), states.data()};
	Vulkan::init_pipeline_blending_info(VK_FALSE, VK_LOGIC_OP_COPY, &mbf_states, &blending_info);

	auto ppln_lyt = i.value().find("pipeline_layout");
	std::vector<std::string> set_lyt = ppln_lyt.value().find("descriptor_set_layouts").value();

	Memory_Buffer_View<VkDescriptorSetLayout> layouts = {(u32)set_lyt.size(), ALLOCA_T(VkDescriptorSetLayout, set_lyt.size())};
	for (u32 i = 0; i < layouts.count; ++i)
	{
	    R_Mem<VkDescriptorSetLayout> l = get_memory(init_const_str(set_lyt[i].c_str(), set_lyt[i].length()));
	    layouts[i] = *l.p;
	}

	auto push_k = ppln_lyt.value().find("push_constant");
	VkShaderStageFlags push_k_flags = 0;
	Memory_Buffer_View<VkPushConstantRange> ranges = {};
	if (push_k != ppln_lyt.value().end())
	{
	    std::string push_k_stage_flags_str = push_k.value().find("stage_flags").value();
	    for (u32 l = 0; l < push_k_stage_flags_str.length(); ++l)
	    {
		switch(push_k_stage_flags_str[l])
		{
		case 'v': {push_k_flags |= VK_SHADER_STAGE_VERTEX_BIT; break;}
		case 'f': {push_k_flags |= VK_SHADER_STAGE_FRAGMENT_BIT; break;}
		case 'g': {push_k_flags |= VK_SHADER_STAGE_GEOMETRY_BIT; break;}
		}
	    }
	    u32 push_k_offset = push_k.value().find("offset").value();
	    u32 push_k_size = push_k.value().find("size").value();
	    VkPushConstantRange k_rng = {};
	    Vulkan::init_push_constant_range(push_k_flags, push_k_size, push_k_offset, &k_rng);
	    ranges = Memory_Buffer_View<VkPushConstantRange>{1, &k_rng};
	}
	Vulkan::init_pipeline_layout(&layouts, &ranges, gpu, &new_ppln->layout);

	auto vertex_input_info_json = i.value().find("vertex_input");
	VkPipelineVertexInputStateCreateInfo vertex_input_info = {};
	Vulkan::init_pipeline_vertex_input_info(null_buffer<VkVertexInputBindingDescription>()
						    , null_buffer<VkVertexInputAttributeDescription>());	
	if (vertex_input_info_json != i.value().end())
	{
	    std::string vtx_inp_model = i.value().find("vertex_input").value();
	    if (vtx_inp_model != "")
	    {
		R_Mem<Vulkan::Model> model = get_memory(init_const_str(vtx_inp_model.c_str(), vtx_inp_model.length()));
		Vulkan::init_pipeline_vertex_input_info(model.p, &vertex_input_info);
	    }
	    else
	    {
		vertex_input_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		/* ... */
	    }
	}

	VkPipelineDepthStencilStateCreateInfo depth_stencil_info = {};
	bool enable_depth = i.value().find("depth").value();
	Vulkan::init_pipeline_depth_stencil_info(enable_depth, enable_depth, 0.0f, 1.0f, VK_FALSE, &depth_stencil_info);

	std::vector<u32> extent = i.value().find("viewport").value();
	u32 width = swapchain->extent.width, height = swapchain->extent.height;
	// use swapchain extent
	if (extent[0] != 0 && extent[1] != 0) {width = extent[0]; height = extent[1];}
	VkViewport viewport = {};
	Vulkan::init_viewport(width, height, 0.0f, 1.0f, &viewport);
	VkRect2D scissor = {};
	Vulkan::init_rect_2D(VkOffset2D{}, VkExtent2D{width, height}, &scissor);

	VkPipelineViewportStateCreateInfo viewport_info = {};
	Memory_Buffer_View<VkViewport> viewports = {1, &viewport};
	Memory_Buffer_View<VkRect2D>   scissors = {1, &scissor};
	Vulkan::init_pipeline_viewport_info(&viewports, &scissors, &viewport_info);
	
	// ===== for now, just set to these default values
	VkPipelineMultisampleStateCreateInfo multisample_info = {};
	Vulkan::init_pipeline_multisampling_info(VK_SAMPLE_COUNT_1_BIT, 0, &multisample_info);
	// =====
	
	Memory_Buffer_View<VkPipelineShaderStageCreateInfo> modules = {stg_count, shader_infos};
	auto render_pass_info = i.value().find("render_pass");
	std::string render_pass_name = render_pass_info.value().find("name").value();
	R_Mem<Vulkan::Render_Pass> render_pass = get_memory(init_const_str(render_pass_name.c_str(), render_pass_name.length()));
	u32 subpass = render_pass_info.value().find("subpass").value();
	Vulkan::init_graphics_pipeline(&modules
					   , &vertex_input_info
					   , &assembly_info
					   , &viewport_info
					   , &rasterization_info
					   , &multisample_info
					   , &blending_info
					   , nullptr
					   , &depth_stencil_info
					   , &new_ppln.p->layout
					   , render_pass.p
					   , subpass
					   , gpu
					   , &new_ppln.p->pipeline);
    }
}

internal VkFormat
make_format_from_code(u32 code, Vulkan::Swapchain *swapchain, Vulkan::GPU *gpu)
{
    switch(code)
    {
    case 0: {return(swapchain->format);}
    case 1: {return(VK_FORMAT_D16_UNORM);}
    case 8: {return(VK_FORMAT_R8G8B8A8_UNORM);}
    case 16: {return(VK_FORMAT_R16G16B16A16_SFLOAT);}
    default: {return((VkFormat)0);};
    }
}

void
load_framebuffers_from_json(Vulkan::GPU *gpu
			    , Vulkan::Swapchain *swapchain)
{
    persist const char *filename = "config/fbos.json";

    std::ifstream is (filename);
    is.seekg(0, is.end);
    u32 length = is.tellg();
    is.seekg(0, is.beg);

    char *buffer = new char[sizeof(char) * (length + 1)];
    memset(buffer, 0, length * sizeof(char));
    is.read(buffer, length);
    buffer[length] = '\0';

    std::string content = std::string(buffer, length + 1);
    
    nlohmann::json json = nlohmann::json::parse(content);
    for (auto i = json.begin(); i != json.end(); ++i)
    {
	std::string fbo_name = i.key();
	
	bool insert_swapchain_imgs_at_0 = i.value().find("insert_swapchain_imgs_at_0").value();
	u32 fbos_to_create = (insert_swapchain_imgs_at_0 ? swapchain->imgs.count : 1);
	// create color attachments and depth attachments
	struct Attachment {R_Mem<Vulkan::Image2D> img; u32 index;};
	//	Memory_Buffer_View<Attachment> color_imgs = {};
	std::vector<Attachment> color_imgs;
	color_imgs.resize(i.value().find("color_attachment_count").value());
	//	allocate_memory_buffer(color_imgs, i.value().find("color_attachment_count").value());
	auto color_attachment_node = i.value().find("color_attachments");

	persist auto create_attachment = [&gpu, &swapchain](const Constant_String &name
							    , VkFormat format
							    , VkImageUsageFlags usage
							    , u32 width
							    , u32 height
							    , u32 index
							    , bool make_new) -> Attachment
	    {
		R_Mem<Vulkan::Image2D> img;
		
		if (make_new)
		{
		    img = register_memory(name, sizeof(Vulkan::Image2D));
		}
		else
		{
		    img = get_memory(name);
		}

		Vulkan::init_framebuffer_attachment(width
						    , height
						    , format
						    , usage
						    , gpu
						    , img.p);
		return(Attachment{img, index});
	    };
	
	persist auto create_usage_flags = [](std::string &s) -> VkImageUsageFlags
	{
	    VkImageUsageFlags u = 0;
	    for (u32 c = 0; c < s.length(); ++c)
	    {
		switch(s[c])
		{
		case 'c': {u |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT; break;}
		case 'i': {u |= VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT; break;}
		case 'd': {u |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT; break;}
		case 's': {u |= VK_IMAGE_USAGE_SAMPLED_BIT; break;}
		}
	    }
	    return(u);
	};

	// if these are 0, use swapchain format
	u32 width = i.value().find("width").value();
	u32 height = i.value().find("width").value();
	if (!width || !height) {width = swapchain->extent.width; height = swapchain->extent.height;};
	
	u32 attachment = (insert_swapchain_imgs_at_0 ? 1 : 0);
	if (color_attachment_node != i.value().end())
	{
	    for (auto c = color_attachment_node.value().begin(); c != color_attachment_node.value().end(); ++c, ++attachment)
	    {
		std::string img_name = c.key();
		// fetch data from nodes
		bool to_create = c.value().find("to_create").value();
		u32 index = c.value().find("index").value();
		u32 format = c.value().find("format").value();
		std::string usage = c.value().find("usage").value();
		bool make_new_img = c.value().find("new").value();

		if (to_create)
		{
		    VkFormat f = make_format_from_code(format, swapchain, gpu);

		    VkImageUsageFlags u = create_usage_flags(usage);
	    
		    // use data from nodes
		    if (to_create)
		    {
			color_imgs[attachment] = create_attachment(init_const_str(img_name.c_str(), img_name.length())
								   , f
								   , u
								   , width
								   , height
								   , index
								   , make_new_img);
		    }

		    bool sampler = c.value().find("sampler") != c.value().end();
		    if (sampler)
		    {
			Vulkan::init_image_sampler(VK_FILTER_LINEAR
						   , VK_FILTER_LINEAR
						   , VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
						   , VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
						   , VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
						   , 0
						   , 16
						   , VK_BORDER_COLOR_INT_OPAQUE_BLACK
						   , VK_TRUE
						   , (VkCompareOp)0
						   , VK_SAMPLER_MIPMAP_MODE_LINEAR
						   , 0.0f, 0.0f, 0.0f
						   , gpu
						   , &color_imgs[attachment].img->image_sampler);
		    }
		}
		else
		{
		    R_Mem<Vulkan::Image2D> img = get_memory(init_const_str(img_name.c_str(), img_name.length()));
		    color_imgs[attachment] = Attachment{img, index};
		}
	    }
	}

	std::sort(color_imgs.begin()
		  , color_imgs.end()
		  , [](Attachment &a, Attachment &b) {return(a.index < b.index);});
	
	Attachment depth = {};
	bool enable_depth = i.value().find("depth_attachment") != i.value().end();
	if (enable_depth)
	{
	    auto depth_att_info = i.value().find("depth_attachment");
	    std::string depth_att_name = depth_att_info.value().find("name").value();
	    bool depth_att_to_create = depth_att_info.value().find("to_create").value();
	    u32 depth_att_index = depth_att_info.value().find("index").value();
	    std::string depth_att_usage = depth_att_info.value().find("usage").value();
	    bool make_new_dep = depth_att_info.value().find("new").value();

	    if (depth_att_to_create)
	    {
		depth = create_attachment(init_const_str(depth_att_name.c_str(), depth_att_name.length())
					  , gpu->supported_depth_format
					  , create_usage_flags(depth_att_usage)
					  , width
					  , height
					  , depth_att_index
					  , make_new_dep);

		bool sampler = depth_att_info.value().find("sampler") != depth_att_info.value().end();
		if (sampler)
		{
		    Vulkan::init_image_sampler(VK_FILTER_LINEAR
					       , VK_FILTER_LINEAR
					       , VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
					       , VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
					       , VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
					       , VK_FALSE
					       , 1
					       , VK_BORDER_COLOR_INT_OPAQUE_WHITE
					       , VK_FALSE
					       , (VkCompareOp)0
					       , VK_SAMPLER_MIPMAP_MODE_LINEAR
					       , 0.0f, 0.0f, 1.0f
					       , gpu
					       , &depth.img->image_sampler);
		}
	    }
	    else
	    {
		depth.img = get_memory(init_const_str(depth_att_name.c_str(), depth_att_name.length()));
		depth.index = depth_att_index;
	    }

	    
	}
	
	std::string compatible_render_pass_name = i.value().find("compatible_render_pass").value();
	R_Mem<Vulkan::Render_Pass> compatible_render_pass = get_memory(init_const_str(compatible_render_pass_name.c_str(), compatible_render_pass_name.length()));
	// actual creation of the FBO
	u32 fbo_count = (insert_swapchain_imgs_at_0 ? swapchain->imgs.count /*is for presenting*/ : 1);

	bool make_new = i.value().find("new").value();

	R_Mem<Vulkan::Framebuffer> fbos;
	
	if (make_new)
	{
	    fbos = register_memory(init_const_str(fbo_name.c_str(), fbo_name.length())
				   , sizeof(Vulkan::Framebuffer) * fbo_count);
	}
	else
	{
	    fbos = get_memory(init_const_str(fbo_name.c_str(), fbo_name.length()));
	}

	auto layers_node = i.value().find("layers");
	u32 layers = 1;
	if (layers_node != i.value().end())
	{
	    layers = layers_node.value();
	}
	
	for (u32 fbo = 0; fbo < fbo_count; ++fbo)
	{
	    allocate_memory_buffer(fbos.p[fbo].color_attachments
				   , color_imgs.size());

	    for (u32 color = 0; color < color_imgs.size(); ++color)
	    {
		if (insert_swapchain_imgs_at_0 && color == 0)
		{
		    fbos.p[fbo].color_attachments[color] = swapchain->views[fbo];
		}
		else
		{
		    fbos.p[fbo].color_attachments[color] = color_imgs[color].img.p->image_view;
		}
	    }

	    if (enable_depth)
	    {
		fbos.p[fbo].depth_attachment = depth.img.p->image_view;
	    }

	    Vulkan::init_framebuffer(compatible_render_pass.p
					 , width
					 , height
					 , layers
					 , gpu
					 , &fbos.p[fbo]);
	}
    }
}

internal VkAttachmentLoadOp
make_load_op_from_code(char code)
{
    switch(code)
    {
    case 'c': {return(VK_ATTACHMENT_LOAD_OP_CLEAR);}
    case 'd': {return(VK_ATTACHMENT_LOAD_OP_DONT_CARE);}
    default: {return((VkAttachmentLoadOp)0);}
    }
}

internal VkAttachmentStoreOp
make_store_op_from_code(char code)
{
    switch(code)
    {
    case 's': {return(VK_ATTACHMENT_STORE_OP_STORE);}
    case 'd': {return(VK_ATTACHMENT_STORE_OP_DONT_CARE);}
    default: {return((VkAttachmentStoreOp)0);}
    }
}

internal VkImageLayout
make_image_layout_from_code(char code)
{
    switch(code)
    {
    case 'u': {return(VK_IMAGE_LAYOUT_UNDEFINED);}
    case 'p': {return(VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);}
    case 'c': {return(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);}
    case 'd': {return(VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);}
    case 'r': {return(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);}
    case 's': {return(VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);}
    default: {return((VkImageLayout)0);}
    }
}

internal VkPipelineStageFlags
make_pipeline_stage_flags_from_code(const char *s, u32 len)
{
    VkPipelineStageFlags f = 0;
    for (u32 i = 0; i < len; ++i)
    {
	switch(s[i])
	{
	case 'b': {f = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT; break;}
	case 'o': {f = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT; break;}
	case 'f': {f = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT; break;}
	case 'e': {f = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT; break;}
	case 'l': {f = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT; break;}
	}
    }
    return(f);
}

internal u32
make_gpu_memory_access_flags_from_code(const char *s, u32 len)
{
    u32 f = 0;
    
    char type = s[0];
    persist auto get_correct_read_flag = [&type, &f](void)
    {
	switch(type)
	{
	case 'c': {f |= VK_ACCESS_COLOR_ATTACHMENT_READ_BIT; break;}
	case 's': {f |= VK_ACCESS_SHADER_READ_BIT; break;}
	case 'm': {f |= VK_ACCESS_MEMORY_READ_BIT; break;}
	}
    };
    persist auto get_correct_write_flag = [&type, &f](void)
    {
	switch(type)
	{
	case 'c': {f |= VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT; break;}
	case 'd': {f |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT; break;}
	case 's': {f |= VK_ACCESS_SHADER_WRITE_BIT; break;}
	case 'm': {f |= VK_ACCESS_MEMORY_WRITE_BIT; break;}
	}
    };
    
    for (u32 i = 1; i < len; ++i)
    {
	switch(s[i])
	{
	case 'r': {get_correct_read_flag(); break;}
	case 'w': {get_correct_write_flag(); break;}
	}
    }
    return(f);
}

#include <fstream>

void
load_render_passes_from_json(Vulkan::GPU *gpu
			     , Vulkan::Swapchain *swapchain)
{
    persist const char *filename = "config/render_passes.json";

    std::ifstream is (filename);
    is.seekg(0, is.end);
    u32 length = is.tellg();
    is.seekg(0, is.beg);

    char *buffer = new char [sizeof(char) * length];
    memset(buffer, 0, length * sizeof(char));
    is.read(buffer, length);

    std::string content = std::string(buffer, length);
    
    nlohmann::json json = nlohmann::json::parse(content);
    for (auto i = json.begin(); i != json.end(); ++i)
    {
	std::string rndr_pass_name = i.key();
	u32 color_attachment_count = i.value().find("attachment_count").value();
	VkAttachmentDescription *att_descriptions = ALLOCA_T(VkAttachmentDescription, color_attachment_count);

	u32 current_att = 0;
	auto color_attachments_info = i.value().find("color_attachments");

	persist auto make_attachment_description = [&att_descriptions, &swapchain, &gpu, &current_att](auto a) -> void
	{
	    u32 format_code = a.value().find("format").value();
	    std::string color_load_str = a.value().find("color_load").value();
	    std::string color_store_str = a.value().find("color_store").value();
	    std::string depth_load_str = a.value().find("depth_load").value();
	    std::string depth_store_str = a.value().find("depth_store").value();
	    std::string initial_layout_str = a.value().find("initial_layout").value();
	    std::string final_layout_str = a.value().find("final_layout").value();

	    VkFormat format = make_format_from_code(format_code, swapchain, gpu);
	    VkAttachmentLoadOp color_load_op = make_load_op_from_code(color_load_str[0]);
	    VkAttachmentStoreOp color_store_op = make_store_op_from_code(color_store_str[0]);
	    VkAttachmentLoadOp depth_load_op = make_load_op_from_code(depth_load_str[0]);
	    VkAttachmentStoreOp depth_store_op = make_store_op_from_code(depth_store_str[0]);
	    VkImageLayout initial_layout = make_image_layout_from_code(initial_layout_str[0]);
	    VkImageLayout final_layout = make_image_layout_from_code(final_layout_str[0]);

	    att_descriptions[current_att] = Vulkan::init_attachment_description(format
										    , VK_SAMPLE_COUNT_1_BIT
										    , color_load_op
										    , color_store_op
										    , depth_load_op
										    , depth_store_op
										    , initial_layout
										    , final_layout);
	};
	
	if (color_attachments_info != i.value().end())
	{
	    // has color attachments
	    for (auto a = color_attachments_info.value().begin(); a != color_attachments_info.value().end(); ++a, ++current_att)
	    {
		make_attachment_description(a);
	    }
	}
	auto depth_attachment_info = i.value().find("depth_attachment");
	if (depth_attachment_info != i.value().end())
	{
	    // has depth attachment
	    make_attachment_description(depth_attachment_info);
	    ++current_att;
	}

	// start making the subpass descriptions
	auto subpass_descriptions_info = i.value().find("subpasses");
	u32 subpass_count = subpass_descriptions_info.value().size();
	VkSubpassDescription *subpass_descriptions = ALLOCA_T(VkSubpassDescription, subpass_count);
	u32 current_subpass = 0;
	for (auto s = subpass_descriptions_info.value().begin(); s != subpass_descriptions_info.value().end(); ++s, ++current_subpass)
	{
	    persist auto make_attachment_reference = [](auto info) -> VkAttachmentReference
	    {
		u32 index = info.value().find("index").value();
		std::string layout_str = info.value().find("layout").value();
		VkImageLayout layout = make_image_layout_from_code(layout_str[0]);
		return(Vulkan::init_attachment_reference(index, layout));
	    };
	    
	    auto color_out = s.value().find("color_out");
	    auto depth_out = s.value().find("depth_out");
	    auto in = s.value().find("in");

	    Memory_Buffer_View<VkAttachmentReference> c = {}, d = {}, i = {};
	    if (color_out != s.value().end())
	    {
		// subpass references color attachments
		c.count = color_out.value().size();
		c.buffer = ALLOCA_T(VkAttachmentReference, c.count);
		u32 current_c = 0;
		for (auto c_info = color_out.value().begin(); c_info != color_out.value().end(); ++c_info, ++current_c)
		{
		    c[current_c] = make_attachment_reference(c_info);
		}
	    }
	    if (depth_out != s.value().end())
	    {
		// subpass references depth attachment
		d.count = 1;
		d.buffer = ALLOCA_T(VkAttachmentReference, 1);
		d[0] = make_attachment_reference(depth_out);
	    }
	    if (in != s.value().end())
	    {
		// subpass references input attachments
		i.count = in.value().size();
		i.buffer = ALLOCA_T(VkAttachmentReference, i.count);
		u32 current_i = 0;
		for (auto i_info = in.value().begin(); i_info != in.value().end(); ++i_info, ++current_i)
		{
		    i[current_i] = make_attachment_reference(i_info);
		}
	    }

	    // make the subpass description
	    subpass_descriptions[current_subpass] = Vulkan::init_subpass_description(c
										     , d.buffer
										     , i);
	}

	// start making the dependencies
	auto dependency_info = i.value().find("dependencies");
	u32 dependency_count = dependency_info.value().size();
	u32 current_d = 0;
	VkSubpassDependency *dependencies = ALLOCA_T(VkSubpassDependency, dependency_count);
	for (auto d = dependency_info.value().begin(); d != dependency_info.value().end(); ++d, ++current_d)
	{
	    u32 src = d.value().find("src").value();
	    if (src == -1)
	    {
		src = VK_SUBPASS_EXTERNAL;
	    }
	    u32 dst = d.value().find("dst").value();
	    if (dst == -1)
	    {
		dst = VK_SUBPASS_EXTERNAL;
	    }
	    std::string src_access = d.value().find("src_access").value();
	    std::string dst_access = d.value().find("dst_access").value();
	    u32 src_access_mask = make_gpu_memory_access_flags_from_code(src_access.c_str(), src_access.length());
	    u32 dst_access_mask = make_gpu_memory_access_flags_from_code(dst_access.c_str(), dst_access.length());
	    std::string src_stage_str = d.value().find("src_stage").value();
	    std::string dst_stage_str = d.value().find("dst_stage").value();
	    VkPipelineStageFlags src_stage = make_pipeline_stage_flags_from_code(src_stage_str.c_str(), src_stage_str.length());
	    VkPipelineStageFlags dst_stage = make_pipeline_stage_flags_from_code(dst_stage_str.c_str(), dst_stage_str.length());

	    // to parameterise later - too tired
	    VkDependencyFlagBits f = VK_DEPENDENCY_BY_REGION_BIT;
	    
	    dependencies[current_d] = Vulkan::init_subpass_dependency(src
									  , dst
									  , src_stage
									  , src_access_mask
									  , dst_stage
									  , dst_access_mask
									  , f);
	}

	bool make_new = i.value().find("new").value();
	R_Mem<Vulkan::Render_Pass> new_rndr_pass;
	if (make_new)
	{
	    new_rndr_pass = register_memory(init_const_str(rndr_pass_name.c_str(), rndr_pass_name.length())
					    , sizeof(Vulkan::Render_Pass));
	}
	else
	{
	    new_rndr_pass = get_memory(init_const_str(rndr_pass_name.c_str(), rndr_pass_name.length()));
	}
	
	Vulkan::init_render_pass(Memory_Buffer_View<VkAttachmentDescription>{color_attachment_count, att_descriptions}
				     , Memory_Buffer_View<VkSubpassDescription>{subpass_count, subpass_descriptions}
				     , Memory_Buffer_View<VkSubpassDependency>{dependency_count, dependencies}
				     , gpu
				     , new_rndr_pass.p);
    }
}

void
load_descriptors_from_json(Vulkan::GPU *gpu
			   , Vulkan::Swapchain *swapchain)
{
    persist const char *filename = "config/desc.json";
    
    std::ifstream is (filename);
    is.seekg(0, is.end);
    u32 length = is.tellg();
    is.seekg(0, is.beg);

    char *buffer = new char[sizeof(char) * length];
    memset(buffer, 0, length * sizeof(char));
    is.read(buffer, length);

    std::string content = std::string(buffer, length);
    
    nlohmann::json json = nlohmann::json::parse(content);
    

    // ---- caches for descriptor set information ----
    VkDescriptorImageInfo image_infos_cache [10];
    VkDescriptorBufferInfo buffer_infos_cache [10];

    std::string set_or_layout[] = {"layout", "set"};

    for (u32 sol = 0; sol < 2; ++sol)
    {
	for (auto i = json.begin(); i != json.end(); ++i)
	{
	    std::string key = i.key();
	    
	    if (i.value().find("type").value() == "layout" && set_or_layout[sol] == "layout")
	    {
		// initialize a descriptor set layout
		bool make_new = i.value().find("new").value();
		R_Mem<VkDescriptorSetLayout> new_descriptor_set_layout;
		if (make_new)
		{
		    new_descriptor_set_layout = register_memory(init_const_str(key.c_str(), key.length())
								, sizeof(VkDescriptorSetLayout));
		}
		else
		{
		    new_descriptor_set_layout = get_memory(init_const_str(key.c_str(), key.length()));
		}
		
		u32 binding_count = i.value().find("binding_count").value();
		VkDescriptorSetLayoutBinding *bindings;
		if (binding_count > 0)
		{
		    bindings = ALLOCA_T(VkDescriptorSetLayoutBinding, binding_count);
		}
		else
		{
		    bindings = nullptr;
		}
		
		auto bindings_node = i.value().find("bindings");
		
		u32 at = 0;
		for (auto binding_description = bindings_node.value().begin()
			 ; binding_description != bindings_node.value().end()
			 ; ++binding_description, ++at)
		{
		    char type = std::string(binding_description.value().find("type").value())[0];
		    VkDescriptorType vk_type;
		    u32 index = binding_description.value().find("binding").value();
		    u32 count = binding_description.value().find("count").value();
		    char flags = std::string(binding_description.value().find("shader_stages").value())[0];
		    VkShaderStageFlagBits vk_flags;
		    
		    switch(type)
		    {
		    case 'c': {vk_type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; break;}
		    case 'i': {vk_type = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT; break;}
		    }
		    
		    switch(flags)
		    {
		    case 'v': {vk_flags = VK_SHADER_STAGE_VERTEX_BIT; break;}
		    case 'f': {vk_flags = VK_SHADER_STAGE_FRAGMENT_BIT; break;}
		    case 'g': {vk_flags = VK_SHADER_STAGE_GEOMETRY_BIT; break;}
		    case 't': {break;}
		    };
		    
		    bindings[at] = Vulkan::init_descriptor_set_layout_binding(vk_type, index, count, vk_flags);
		}
		
		Memory_Buffer_View<VkDescriptorSetLayoutBinding> bindings_mbv {binding_count, bindings};
		Vulkan::init_descriptor_set_layout(bindings_mbv, gpu, new_descriptor_set_layout.p);
	    }
	    else if(i.value().find("type").value() == "set" && set_or_layout[sol] == "set")
	    {
		std::string pool_name = i.value().find("source_pool").value();
		R_Mem<VkDescriptorPool> source_pool = get_memory(init_const_str(pool_name.c_str(), pool_name.length()));

		s32 count = i.value().find("count").value();
		if (count < 0)
		{
		    count = swapchain->imgs.count;
		}
		
		std::string layout_name = i.value().find("layout").value();
		R_Mem<VkDescriptorSetLayout> layout = get_memory(init_const_str(layout_name.c_str(), layout_name.length()));
		
		VkDescriptorSetLayout *layouts_inline = ALLOCA_T(VkDescriptorSetLayout, count);
		for (u32 l = 0; l < count; ++l) 
		{
		    layouts_inline[l] = *layout.p;
		}
		
		bool make_new = i.value().find("new").value();
		R_Mem<Vulkan::Descriptor_Set> sets = [&make_new, &key, &count]
		    {
			if (make_new)
			{
			    return(register_memory(init_const_str(key.c_str(), key.length()), sizeof(Vulkan::Descriptor_Set) * count));
			}
			else
			{
			    return(get_memory(init_const_str(key.c_str(), key.length())));
			}
		    }();
		
		Vulkan::Descriptor_Set **sets_inline = ALLOCA_T(Vulkan::Descriptor_Set *, count);
		for (u32 s = 0; s < count; ++s)
		{
		    sets_inline[s] = &sets.p[s];
		}
		
		Memory_Buffer_View<Vulkan::Descriptor_Set *> separate_sets = {(u32)count, sets_inline};
		Vulkan::allocate_descriptor_sets(separate_sets
						 , Memory_Buffer_View<VkDescriptorSetLayout>{(u32)count, layouts_inline}
						 , gpu
						 , source_pool.p);
	    
		
		
		auto bindings_node = i.value().find("bindings");

		for (u32 set_i = 0; set_i < count; ++set_i)
		{
		    VkWriteDescriptorSet *writes = ALLOCA_T(VkWriteDescriptorSet, bindings_node.value().size());
		    u32 write_index = 0;
		    
		    u32 buffer_info_cache_index = 0;
		    u32 image_info_cache_index = 0;
		    
		    for (auto binding_desc = bindings_node.value().begin()
			     ; binding_desc != bindings_node.value().end()
			     ; ++binding_desc, ++write_index)
		    {
			std::string type_str = binding_desc.value().find("type").value();
			u32 dst_element = binding_desc.value().find("dst_element").value();
			u32 objects_count = binding_desc.value().find("count").value();
			u32 binding = binding_desc.value().find("binding").value();
			
			if (type_str == "buffer")
			{
			    auto buffer_info = binding_desc.value().find("buffer");
			    std::string buffer_name = buffer_info.value().find("name").value();
			    s32 at = buffer_info.value().find("at").value();
			    
			    R_Mem<Vulkan::Buffer> buffers = get_memory(init_const_str(buffer_name.c_str(), buffer_name.length()));
			    Vulkan::Buffer *buffer = [&at, &set_i, &buffers]
				{
				    if (at < 0)
				    {
					return(&buffers.p[set_i]);
				    }
				    else
				    {
					return(&buffers.p[at]);
				    }
				}();
			    
			    u32 buffer_offset = binding_desc.value().find("buffer_offset").value();
			    
			    buffer_infos_cache[buffer_info_cache_index] = {};
			    Vulkan::init_descriptor_set_buffer_info(buffer, buffer_offset, &buffer_infos_cache[buffer_info_cache_index]);
			    writes[write_index] = {};
			    Vulkan::init_buffer_descriptor_set_write(&sets.p[set_i]
								     , binding
								     , dst_element
								     , objects_count
								     , &buffer_infos_cache[buffer_info_cache_index]
								     , &writes[write_index]);
			    
			    ++buffer_info_cache_index;
			}
			else if (type_str == "input")
			{
			    auto image_info = binding_desc.value().find("image");
			    std::string image_name = image_info.value().find("name").value();
			    
			    char layout_code = std::string(image_info.value().find("layout").value())[0];
			    VkImageLayout layout = make_image_layout_from_code(layout_code);
			    
			    R_Mem<Vulkan::Image2D> image = get_memory(init_const_str(image_name.c_str(), image_name.length()));

			    image_infos_cache[image_info_cache_index] = {};
			    Vulkan::init_descriptor_set_image_info(image->image_sampler, image->image_view, layout, &image_infos_cache[image_info_cache_index]);

			    writes[write_index] = {};
			    Vulkan::init_input_attachment_descriptor_set_write(&sets.p[set_i]
									       , binding_desc.value().find("binding").value()
									       , binding_desc.value().find("dst_element").value()
									       , binding_desc.value().find("count").value()
									       , &image_infos_cache[image_info_cache_index]
									       , &writes[write_index]);
			    
			    ++image_info_cache_index;
			}
			else if (type_str == "sampler")
			{
			    auto image_info = binding_desc.value().find("image");
			    std::string image_name = image_info.value().find("name").value();
			    
			    char layout_code = std::string(image_info.value().find("layout").value())[0];
			    VkImageLayout layout = make_image_layout_from_code(layout_code);
			    
			    R_Mem<Vulkan::Image2D> image = get_memory(init_const_str(image_name.c_str(), image_name.length()));

			    image_infos_cache[image_info_cache_index] = {};
			    Vulkan::init_descriptor_set_image_info(image->image_sampler, image->image_view, layout, &image_infos_cache[image_info_cache_index]);

			    writes[write_index] = {};
			    Vulkan::init_image_descriptor_set_write(&sets.p[set_i]
								    , binding_desc.value().find("binding").value()
								    , binding_desc.value().find("dst_element").value()
								    , binding_desc.value().find("count").value()
								    , &image_infos_cache[image_info_cache_index]
								    , &writes[write_index]);
			    
			    ++image_info_cache_index;
			}
		    }
		    
		    Vulkan::update_descriptor_sets(Memory_Buffer_View<VkWriteDescriptorSet>{(u32)(bindings_node.value().size()), writes}
					       , gpu);
		}	    
	    }
	}
    }
}
