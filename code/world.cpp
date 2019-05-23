#include <chrono>
#include <iostream>
#include "load.hpp"
#include "world.hpp"
#include <glm/glm.hpp>
#include <GLFW/glfw3.h>
#include <glm/gtx/transform.hpp>
#include <glm/gtc/quaternion.hpp>


#define MAX_ENTITIES_UNDER_TOP 10
#define MAX_ENTITIES_UNDER_PLANET 150

constexpr f32 PI = 3.14159265359f;

global_var struct World
{
    Camera user_camera;
} world;

// almost acts like the actual render component object itself
struct Material
{
    // ---- push constant information
    void *push_k_ptr = nullptr;
    u32 push_k_size = 0;
    // ---- vbo information
    Memory_Buffer_View<VkBuffer> vbo_bindings;
    // ---- for sorting
    u32 model_id;
    // ---- ibo information
    Vulkan::Model_Index_Data index_data;
    Vulkan::Draw_Indexed_Data draw_index_data;
};

// ---- todo in the future ----
// sort materials out with their models, make renderers handle an array of textures that the user passes in or something if materials use different textures
// ----
struct Render_Command_Recorder
{
    R_Mem<Vulkan::Graphics_Pipeline> ppln;
    VkShaderStageFlags push_k_dst;
	
    Memory_Buffer_View<Material> mtrls;
    u32 mtrl_count;

    u32 cmdbuf_index;

    void
    init(VkCommandBuffer *cmdbufs_pool
	 , u32 &active_cmdbuf_count
	 , R_Mem<Vulkan::Graphics_Pipeline> p_ppln
	 , VkShaderStageFlags pk_dst
	 , u32 max_mtrls
	 , VkCommandPool *cmdpool
	 , Vulkan::GPU *gpu)
    {
	mtrl_count = 0;
	allocate_memory_buffer(mtrls, max_mtrls);

	this->ppln = p_ppln;
	push_k_dst = pk_dst;

	Vulkan::allocate_command_buffers(cmdpool
					 , VK_COMMAND_BUFFER_LEVEL_SECONDARY
					 , gpu
					 , Memory_Buffer_View<VkCommandBuffer>{1, &cmdbufs_pool[active_cmdbuf_count]});

	cmdbuf_index = active_cmdbuf_count++;
    }

    u32 // <---- index of the mtrl in the array
    add(void *push_k_ptr, u32 push_k_size
	, const Memory_Buffer_View<VkBuffer> &vbo_bindings
	, const Vulkan::Model_Index_Data &index_data
	, const Vulkan::Draw_Indexed_Data &draw_index_data) 
    {
	Material new_mtrl		= {};
	new_mtrl.push_k_ptr		= push_k_ptr;
	new_mtrl.push_k_size	= push_k_size;
	new_mtrl.vbo_bindings	= vbo_bindings;
	new_mtrl.index_data		= index_data;
	new_mtrl.draw_index_data	= draw_index_data;

	mtrls[mtrl_count] = new_mtrl;

	return(mtrl_count++);
    }

    void
    update(VkCommandBuffer *cmdbufs_pool, const Memory_Buffer_View<VkDescriptorSet> &sets)
    {
	Vulkan::begin_command_buffer(&cmdbufs_pool[cmdbuf_index], 0, nullptr);
	    
	Vulkan::command_buffer_bind_pipeline(ppln.p, &cmdbufs_pool[cmdbuf_index]);

	Vulkan::command_buffer_bind_descriptor_sets(ppln.p
						    , sets
						    , &cmdbufs_pool[cmdbuf_index]);

	for (u32 i = 0; i < mtrl_count; ++i)
	{
	    Material *mtrl = &mtrls[i];

	    Memory_Buffer_View<VkDeviceSize> offsets;
	    allocate_memory_buffer_tmp(offsets, mtrl->vbo_bindings.count);
	    offsets.zero();
			
	    Vulkan::command_buffer_bind_vbos(mtrl->vbo_bindings
					     , offsets
					     , 0
					     , mtrl->vbo_bindings.count
					     , &cmdbufs_pool[cmdbuf_index]);
			
	    Vulkan::command_buffer_bind_ibo(mtrl->index_data
					    , &cmdbufs_pool[cmdbuf_index]);

	    Vulkan::command_buffer_push_constant(mtrl->push_k_ptr
						 , mtrl->push_k_size
						 , 0
						 , push_k_dst
						 , ppln.p
						 , &cmdbufs_pool[cmdbuf_index]);

	    Vulkan::command_buffer_draw_indexed(&cmdbufs_pool[cmdbuf_index]
						, mtrl->draw_index_data);
	}
	    
	Vulkan::end_command_buffer(&cmdbufs_pool[cmdbuf_index]);
    }
};    


global_var struct World_Rendering_Master_Data
{
    // all the different render passes for the world (e.g. shadow, other post processing stuff...)
    struct Deferred_Rendering_Data
    {
	R_Mem<Vulkan::Render_Pass> render_pass;
	R_Mem<Vulkan::Graphics_Pipeline> lighting_pipeline;
	R_Mem<Vulkan::Graphics_Pipeline> main_pipeline;
	R_Mem<Vulkan::Descriptor_Set> descriptor_set;
	R_Mem<Vulkan::Framebuffer> fbos;
    } deferred;

    struct Atmosphere_Data
    {
	R_Mem<Vulkan::Render_Pass> make_render_pass;
	R_Mem<Vulkan::Framebuffer> make_fbo;
	R_Mem<Vulkan::Graphics_Pipeline> make_pipeline;
	R_Mem<Vulkan::Graphics_Pipeline> render_pipeline;
	R_Mem<Vulkan::Descriptor_Set> cubemap_set;
	R_Mem<VkDescriptorSetLayout> render_layout;
	R_Mem<VkDescriptorSetLayout> make_layout;
    } atmosphere;

    struct Camera_Transforms
    {
	// ---- buffers containing view matrix and projection matrix - basically data that is common to most shaders ----
	R_Mem<Vulkan::Buffer> master_ubos;
    } transforms;

    struct Test
    {
	R_Mem<VkDescriptorSetLayout> set_layout;
	R_Mem<Vulkan::Model> model;
	R_Mem<Vulkan::Buffer> model_vbo;
	R_Mem<Vulkan::Buffer> model_ibo;
	R_Mem<Vulkan::Descriptor_Set> sets;
    } test;

    struct Descriptors
    {
	Vulkan::Descriptor_Pool pool;
    } desc;

    

    // ---- renderers ----
    static constexpr u32 MAX_COMMAND_BUFFERS_IN_USE = 10;
    VkCommandBuffer recording_buffer_pool[MAX_COMMAND_BUFFERS_IN_USE];
    u32 active_cmdbuf_count;
    
    static constexpr u32 MAX_TEST_MTRLS = 10;
    Render_Command_Recorder player_recorder; // <--- value should increment for every renderer the user adds

    Render_Command_Recorder terrain_recorder;
} world_rendering;

struct Instanced_Render_Command_Recorder
{
    // ---- todo later on when instanced rendering is needed ----
};



internal void
prepare_external_loading_state(Vulkan::GPU *gpu, Vulkan::Swapchain *swapchain, VkCommandPool *cmdpool)
{
    world_rendering.deferred.render_pass = register_memory("render_pass.deferred_render_pass"_hash, sizeof(Vulkan::Render_Pass));

    // ---- make cube model info ----
    {
	world_rendering.test.model = register_memory("vulkan_model.test_model"_hash, sizeof(Vulkan::Model));
	
	world_rendering.test.model->attribute_count = 3;
	world_rendering.test.model->attributes_buffer = (VkVertexInputAttributeDescription *)allocate_free_list(sizeof(VkVertexInputAttributeDescription) * 3);
	world_rendering.test.model->binding_count = 1;
	world_rendering.test.model->bindings = (Vulkan::Model_Binding *)allocate_free_list(sizeof(Vulkan::Model_Binding));

	struct Vertex { glm::vec3 pos; glm::vec3 color; glm::vec2 uvs; };
	
	// only one binding
	Vulkan::Model_Binding *binding = world_rendering.test.model->bindings;
	binding->begin_attributes_creation(world_rendering.test.model->attributes_buffer);

	binding->push_attribute(0, VK_FORMAT_R32G32B32_SFLOAT, sizeof(Vertex::pos));
	binding->push_attribute(1, VK_FORMAT_R32G32B32_SFLOAT, sizeof(Vertex::color));
	binding->push_attribute(2, VK_FORMAT_R32G32_SFLOAT, sizeof(Vertex::uvs));

	binding->end_attributes_creation();
    }
    
    // ---- make descriptor set layout for rendering the cubes ----
    {
	world_rendering.test.set_layout = register_memory("descriptor_set_layout.test_descriptor_set_layout"_hash, sizeof(VkDescriptorSetLayout));
	
	VkDescriptorSetLayoutBinding bindings[] =
	    {
		Vulkan::init_descriptor_set_layout_binding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, 1, VK_SHADER_STAGE_VERTEX_BIT)
	    };
	
	Vulkan::init_descriptor_set_layout(Memory_Buffer_View<VkDescriptorSetLayoutBinding>{1, bindings}
					       , gpu
					       , world_rendering.test.set_layout.p);
    }

    // ---- make cube vbo ----
    {
	world_rendering.test.model_vbo = register_memory("vbo.test_model_vbo"_hash, sizeof(Vulkan::Buffer));
	
	struct Vertex { glm::vec3 pos, color; glm::vec2 uvs; };

	glm::vec3 gray = glm::vec3(0.2);
	
	f32 radius = 2.0f;
	
	persist Vertex vertices[]
	{
	    {{-radius, -radius, radius	}, gray},
	    {{radius, -radius, radius	}, gray},
	    {{radius, radius, radius	}, gray},
	    {{-radius, radius, radius	}, gray},
												 
	    {{-radius, -radius, -radius	}, gray},
	    {{radius, -radius, -radius	}, gray},
	    {{radius, radius, -radius	}, gray},
	    {{-radius, radius, -radius	}, gray}
	};
	
	auto *main_binding = &world_rendering.test.model->bindings[0];
	    
	Memory_Byte_Buffer byte_buffer{sizeof(vertices), vertices};
	
	Vulkan::invoke_staging_buffer_for_device_local_buffer(byte_buffer
							      , VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
							      , cmdpool
							      , world_rendering.test.model_vbo.p
							      , gpu);

	main_binding->buffer = world_rendering.test.model_vbo->buffer;
	world_rendering.test.model->create_vbo_list();
    }

    // ---- make cube ibo ----
    {
	world_rendering.test.model_ibo = register_memory("ibo.test_model_ibo"_hash, sizeof(Vulkan::Buffer));
	
	persist u32 mesh_indices[] = 
	{
	    0, 1, 2,
	    2, 3, 0,

	    1, 5, 6,
	    6, 2, 1,

	    7, 6, 5,
	    5, 4, 7,
	    
	    3, 7, 4,
	    4, 0, 3,
	    
	    4, 5, 1,
	    1, 0, 4,
	    
	    3, 2, 6,
	    6, 7, 3,
	};

	world_rendering.test.model->index_data.index_type = VK_INDEX_TYPE_UINT32;
	world_rendering.test.model->index_data.index_offset = 0;
	world_rendering.test.model->index_data.index_count = sizeof(mesh_indices) / sizeof(mesh_indices[0]);

	Memory_Byte_Buffer byte_buffer{sizeof(mesh_indices), mesh_indices};
	    
	Vulkan::invoke_staging_buffer_for_device_local_buffer(byte_buffer
							      , VK_BUFFER_USAGE_INDEX_BUFFER_BIT
							      , cmdpool
							      , world_rendering.test.model_ibo.p
							      , gpu);

	world_rendering.test.model->index_data.index_buffer = world_rendering.test.model_ibo->buffer;
    }

    // ---- make descriptor pool ----
    {
	VkDescriptorPoolSize pool_sizes[3] = {};

	Vulkan::init_descriptor_pool_size(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, swapchain->imgs.count + 5, &pool_sizes[0]);
	Vulkan::init_descriptor_pool_size(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, swapchain->imgs.count + 5, &pool_sizes[1]);
	Vulkan::init_descriptor_pool_size(VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 5, &pool_sizes[2]);

    
	Vulkan::init_descriptor_pool(Memory_Buffer_View<VkDescriptorPoolSize>{3, pool_sizes}, swapchain->imgs.count + 10, gpu, &world_rendering.desc.pool);

	register_existing_memory(&world_rendering.desc.pool, "descriptor_pool.test_descriptor_pool"_hash, sizeof(Vulkan::Descriptor_Pool));
    }

    // ---- make the ubos ----
    {
	struct Uniform_Buffer_Object
	{
	    alignas(16) glm::mat4 model_matrix;
	    alignas(16) glm::mat4 view_matrix;
	    alignas(16) glm::mat4 projection_matrix;
	};
	
	u32 uniform_buffer_count = swapchain->imgs.count;
	
	world_rendering.transforms.master_ubos = register_memory("buffer.ubos"_hash, sizeof(Vulkan::Buffer) * uniform_buffer_count);

	VkDeviceSize buffer_size = sizeof(Uniform_Buffer_Object);

	for (u32 i = 0
		 ; i < uniform_buffer_count
		 ; ++i)
	{
	    Vulkan::init_buffer(buffer_size
				, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT
				, VK_SHARING_MODE_EXCLUSIVE
				, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
				, gpu
				, &world_rendering.transforms.master_ubos[i]);
	}
    }
}

// ---- render the skybox ----
internal void
render_skybox(const Memory_Buffer_View<VkBuffer> &cube_vbos
	      , const Memory_Buffer_View<VkDescriptorSet> &sets
	      , const Vulkan::Model_Index_Data &index_data
	      , const Vulkan::Draw_Indexed_Data &draw_index_data
	      , VkCommandBuffer *cmdbuf)
{
    glm::vec3 &camera_position = world.user_camera.p;

    Vulkan::command_buffer_bind_pipeline(world_rendering.atmosphere.render_pipeline.p, cmdbuf);

    Vulkan::command_buffer_bind_descriptor_sets(world_rendering.atmosphere.render_pipeline.p, sets, cmdbuf);

    VkDeviceSize zero = 0;
    Vulkan::command_buffer_bind_vbos(cube_vbos, {1, &zero}, 0, cube_vbos.count, cmdbuf);

    Vulkan::command_buffer_bind_ibo(index_data, cmdbuf);

    struct Skybox_Push_Constant
    {
	glm::mat4 model_matrix;
    } push_k;

    push_k.model_matrix = glm::scale(glm::vec3(1000.0f));

    Vulkan::command_buffer_push_constant(&push_k
					 , sizeof(push_k)
					 , 0
					 , VK_SHADER_STAGE_VERTEX_BIT
					 , world_rendering.atmosphere.render_pipeline.p
					 , cmdbuf);

    Vulkan::command_buffer_draw_indexed(cmdbuf
					, draw_index_data);
}



// ---- to orgnise later on ----
global_var glm::vec3 light_pos = glm::vec3(0.0f, 10.0f, 0.0f);





// ---- update the skybox ----
internal void
update_skybox(VkCommandBuffer *cmdbuf)
{
    VkClearValue clears[] {Vulkan::init_clear_color_color(0, 0.0, 0.0, 0)};
    
    Vulkan::command_buffer_begin_render_pass(world_rendering.atmosphere.make_render_pass.p
					     , world_rendering.atmosphere.make_fbo.p
					     , Vulkan::init_render_area({0, 0}, VkExtent2D{1000, 1000})
					     , Memory_Buffer_View<VkClearValue>{sizeof(clears) / sizeof(clears[0]), clears}
					     , VK_SUBPASS_CONTENTS_INLINE
					     , cmdbuf);
 
    Vulkan::command_buffer_bind_pipeline(world_rendering.atmosphere.make_pipeline.p
					 , cmdbuf);

    struct Atmos_Push_K
    {
	alignas(16) glm::mat4 inverse_projection;
	glm::vec4 light_dir;
	glm::vec2 viewport;
    } k;

    glm::mat4 atmos_proj = glm::perspective(glm::radians(90.0f), 1000.0f / 1000.0f, 0.1f, 10000.0f);
    k.inverse_projection = glm::inverse(atmos_proj);
    k.viewport = glm::vec2(1000.0f, 1000.0f);

    k.light_dir = glm::vec4(glm::normalize(-light_pos), 1.0f);
    
    Vulkan::command_buffer_push_constant(&k
					 , sizeof(k)
					 , 0
					 , VK_SHADER_STAGE_FRAGMENT_BIT
					 , world_rendering.atmosphere.make_pipeline.p
					 , cmdbuf);
    
    Vulkan::command_buffer_draw(cmdbuf
				, 1, 1, 0, 0);

    Vulkan::command_buffer_end_render_pass(cmdbuf);
}

// ---- rendering of the entire world happens here ----
internal void
prepare_terrain_pointer_for_render(VkCommandBuffer *cmdbuf, VkDescriptorSet *set);

internal void
render_world(Vulkan::State *vk
	     , u32 image_index
	     , u32 current_frame
	     , VkCommandBuffer *cmdbuf)
{
    Vulkan::begin_command_buffer(cmdbuf, 0, nullptr);
    
    // ---- update the skybox (in the future, only do when necessary, not every frame) ----
    //    update_skybox(cmdbuf);
    
    // ---- record command buffer for rendering each different type of entity ----
    VkDescriptorSet test_sets[2] = {world_rendering.test.sets[image_index].set
				    , world_rendering.atmosphere.cubemap_set->set};
    
    world_rendering.player_recorder.update(world_rendering.recording_buffer_pool, {2, test_sets}); // <--- recording
    world_rendering.terrain_recorder.update(world_rendering.recording_buffer_pool, {2, test_sets});
    // ---- end of recording command buffers for each different type of entity / material ----



    // ---- initialize the deferred render pass ... ----
    VkClearValue deferred_clears[] = {Vulkan::init_clear_color_color(0, 0.4, 0.7, 0)
				      , Vulkan::init_clear_color_color(0, 0.4, 0.7, 0)
				      , Vulkan::init_clear_color_color(0, 0.4, 0.7, 0)
				      , Vulkan::init_clear_color_color(0, 0.4, 0.7, 0)
				      , Vulkan::init_clear_color_depth(1.0f, 0)};

    // ---- render using deferred render pass ----
    Vulkan::command_buffer_begin_render_pass(world_rendering.deferred.render_pass.p
					     , &world_rendering.deferred.fbos.p[image_index]
					     , Vulkan::init_render_area({0, 0}, vk->swapchain.extent)
					     , {sizeof(deferred_clears) / sizeof(deferred_clears[0]), deferred_clears}
					     , VK_SUBPASS_CONTENTS_INLINE
					     , cmdbuf);

    // ---- execute the commands that were just recorded (rendering the different types of entities)
    Vulkan::command_buffer_execute_commands(cmdbuf, {world_rendering.active_cmdbuf_count, world_rendering.recording_buffer_pool});

    prepare_terrain_pointer_for_render(cmdbuf, &world_rendering.test.sets[image_index].set);
    
    // ---- render skybox ----
    render_skybox({1, world_rendering.test.model->raw_cache_for_rendering.buffer}
	  , {2, test_sets}
	  , world_rendering.test.model->index_data
	  , world_rendering.test.model->index_data.init_draw_indexed_data(0, 0)
	  , cmdbuf);
    
    // ---- do lighting ----
    Vulkan::command_buffer_next_subpass(cmdbuf
					, VK_SUBPASS_CONTENTS_INLINE);
    
    Vulkan::command_buffer_bind_pipeline(world_rendering.deferred.lighting_pipeline.p
					 , cmdbuf);
    
    VkDescriptorSet deferred_sets[] = {world_rendering.deferred.descriptor_set.p->set
				       , world_rendering.atmosphere.cubemap_set.p->set};
    
    Vulkan::command_buffer_bind_descriptor_sets(world_rendering.deferred.lighting_pipeline.p
						, {2, deferred_sets}
						, cmdbuf);
    
    struct Deferred_Lighting_Push_K
    {
	glm::vec4 light_position;
    } deferred_push_k;
    
    deferred_push_k.light_position = glm::vec4(glm::normalize(-light_pos), 1.0f);
    
    Vulkan::command_buffer_push_constant(&deferred_push_k
					 , sizeof(deferred_push_k)
					 , 0
					 , VK_SHADER_STAGE_FRAGMENT_BIT
					 , world_rendering.deferred.lighting_pipeline.p
					 , cmdbuf);
    
    Vulkan::command_buffer_draw(cmdbuf
				, 3, 1, 0, 0);
    
    Vulkan::command_buffer_end_render_pass(cmdbuf);
    // ---- end of deferred render pass ----
    Vulkan::end_command_buffer(cmdbuf);
}

// index into array
typedef struct Entity_View
{
    // may have other data in the future in case we create separate arrays for different types of entities
    s32 id :28;
    
    enum Is_Group { IS_NOT_GROUP = false
		    , IS_GROUP = true } is_group :4;

    Entity_View(void) : id(-1), is_group(Is_Group::IS_NOT_GROUP) {}
} Entity_View, Entity_Group_View;


struct Entity_Base
{
    // position
    glm::vec3 gs_p {0.0f};
    // direction (world space)
    glm::vec3 ws_d {0.0f};
    // velocity
    glm::vec3 gs_v {0.0f};
    // rotation
    glm::quat gs_r {0.0f, 0.0f, 0.0f, 0.0f};

    
};

// ---- terrain code ----
struct Morphable_Terrain
{
    glm::ivec2 xz_dim;
    
    // ---- up vector of the terrain ----
    glm::vec3 ws_n;
    f32 *heights;

    glm::vec3 size;
    glm::vec3 ws_p;
    glm::quat gs_r;

    u32 offset_into_heights_gpu_buffer;
    // ---- later on this will be a pointer
    Vulkan::Buffer heights_gpu_buffer;
    Vulkan::Mapped_GPU_Memory mapped_gpu_heights;

    VkBuffer vbos[2];

    struct Push_K
    {
	glm::mat4 transform;
	glm::vec3 color;
    } push_k;
};

struct Planet
{
    // planet has 6 faces
    Morphable_Terrain meshes[6];

    glm::vec3 p;
    glm::quat r;
};

global_var struct Morphable_Terrain_Master
{
    // ---- X and Z values stored as vec2 (binding 0) ----
    Vulkan::Buffer mesh_xz_values;
    
    Vulkan::Buffer idx_buffer;
    R_Mem<Vulkan::Model> model_info;

    Morphable_Terrain green_mesh;
    Morphable_Terrain red_mesh;

    R_Mem<Vulkan::Graphics_Pipeline> terrain_ppln;

    struct
    {
	R_Mem<Vulkan::Graphics_Pipeline> ppln;
	// ts_position
	glm::ivec2 ts_position{-1};
	// will not be a pointer in the future
	Morphable_Terrain *t;
    } terrain_pointer;
} terrain_master;

inline u32
get_terrain_index(u32 x, u32 z, u32 depth_z)
{
    return(x + z * depth_z);
}

inline s32
get_terrain_index(s32 x, s32 z, s32 width_x, s32 depth_z)
{
    if (x >= 0 && x < width_x
	&& z >= 0 && z < depth_z)
    {
	return(x + z * depth_z);
    }
    else
    {
	return(-1);
    }
}

inline glm::mat4
compute_ws_to_ts_matrix(Morphable_Terrain *t)
{
    glm::mat4 inverse_translate = glm::translate(-(t->ws_p));
    glm::mat4 inverse_rotate = glm::transpose(glm::mat4_cast(t->gs_r));
    glm::mat4 inverse_scale = glm::scale(1.0f / t->size);
    return(inverse_scale * inverse_rotate * inverse_translate);
}

inline glm::vec3
transform_from_ws_to_ts(const glm::vec3 &ws_v
			, Morphable_Terrain *t)
{
    glm::vec3 ts_position = compute_ws_to_ts_matrix(t) * glm::vec4(ws_v, 1.0f);

    return(ts_position);
}

internal bool
is_on_terrain(const glm::vec3 &ws_position
	      , Morphable_Terrain *t)
{
    f32 max_x = (f32)(t->xz_dim.x);
    f32 max_z = (f32)(t->xz_dim.y);
    f32 min_x = 0.0f;
    f32 min_z = 0.0f;

    // ---- change ws_position to the space of the terrain space ----
    glm::vec3 ts_position = transform_from_ws_to_ts(ws_position, t);
    
    // ---- check if terrain space position is in the xz boundaries ----
    bool is_in_x_boundaries = (ts_position.x > min_x && ts_position.x < max_x);
    bool is_in_z_boundaries = (ts_position.z > min_z && ts_position.z < max_z);
    bool is_on_top          = (ts_position.y > 0.0f);

    return(is_in_x_boundaries && is_in_z_boundaries && is_on_top);
}

internal glm::ivec2
get_coord_pointing_at(glm::vec3 ws_ray_p
		      , const glm::vec3 &ws_ray_d
		      , Morphable_Terrain *t
		      , f32 dt
		      , Vulkan::GPU *gpu)
{
    auto get_distance_squared = [](const glm::vec3 &v)
    {
	return(v.x * v.x + v.y * v.y + v.z * v.z);
    };
    
    persist constexpr f32 MAX_DISTANCE = 5.0f;
    persist constexpr f32 MAX_DISTANCE_SQUARED = MAX_DISTANCE * MAX_DISTANCE;
    persist constexpr f32 STEP_SIZE = 0.5f;

    glm::mat4 ws_to_ts = compute_ws_to_ts_matrix(t);
    glm::vec3 ts_ray_p_start = glm::vec3(ws_to_ts * glm::vec4(ws_ray_p, 1.0f));
    glm::vec3 ts_ray_d = glm::normalize(glm::vec3(ws_to_ts * glm::vec4(ws_ray_d, 0.0f)));
    glm::vec3 ts_ray_diff = STEP_SIZE * ts_ray_d;

    glm::ivec2 ts_position = glm::ivec2(-1);
    
    for (glm::vec3 ts_ray_step = ts_ray_d
	     ; get_distance_squared(ts_ray_step) < MAX_DISTANCE_SQUARED
	     ; ts_ray_step += ts_ray_diff)
    {
	glm::vec3 ts_ray_current_p = ts_ray_step + ts_ray_p_start;

	if (ts_ray_current_p.x >= 0.0f && ts_ray_current_p.x < (f32)t->xz_dim.x + 0.000001f
	    && ts_ray_current_p.z >= 0.0f && ts_ray_current_p.z < (f32)t->xz_dim.y + 0.000001f)
	{
	    u32 x = (u32)glm::round(ts_ray_current_p.x / 2.0f) * 2;
	    u32 z = (u32)glm::round(ts_ray_current_p.z / 2.0f) * 2;

	    u32 index = get_terrain_index(x, z, t->xz_dim.y);

	    if (ts_ray_step.y < t->heights[index])
	    {
		// ---- hit terrain at this point ----
		ts_position = glm::ivec2(x, z);
		break;
	    }
	}
    }
    t->mapped_gpu_heights.flush(0, t->mapped_gpu_heights.size, gpu);

    return(ts_position);
}

internal void
morph_terrain_at(const glm::ivec2 &ts_position
		 , Morphable_Terrain *t
		 , f32 dt)
{
    // ---- hit terrain at this point ----
    u32 index = get_terrain_index(ts_position.x, ts_position.y, t->xz_dim.y);
    f32 *p = (f32 *)t->mapped_gpu_heights.data;
    p[index] += dt * 3.0f;
}

internal Morphable_Terrain *
on_which_terrain(const glm::vec3 &ws_position)
{
    // ---- loop through all the terrains ----

    // ---- for testing, go through each terrain individually ----
    bool green = is_on_terrain(ws_position, &terrain_master.green_mesh);
    bool red = is_on_terrain(ws_position, &terrain_master.red_mesh);

    //    if (green) std::cout << "green";
    //    if (red) std::cout << "red";
    
    return(nullptr);
}

// ---- this command happens when rendering (terrain is updated on the cpu side at a different time) ----
internal void
update_terrain_on_gpu(Window_Data *wd
		      , f32 dt)
{
    
}

internal void
make_3D_terrain_base(u32 width_x
		     , u32 depth_z
		     , f32 random_displacement_factor
		     , Vulkan::Buffer *mesh_xz_values
		     , Vulkan::Buffer *idx_buffer
		     , Vulkan::Model *model_info
		     , VkCommandPool *cmdpool
		     , Vulkan::GPU *gpu)
{
    assert(width_x & 0X1 && depth_z & 0X1);
    
    f32 *vtx = (f32 *)allocate_stack(sizeof(f32) * 2 * width_x * depth_z);
    u32 *idx = (u32 *)allocate_stack(sizeof(u32) * 11 * (((width_x - 2) * (depth_z - 2)) / 4));
    
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
	    idx[crnt_idx++] = get_terrain_index(x - 1, z - 1, depth_z);
	    // ---- Vulkan API special value for U32 index type ----
	    idx[crnt_idx++] = 0XFFFFFFFF;
	}
    }
    
    // load data into buffers
    Vulkan::invoke_staging_buffer_for_device_local_buffer(Memory_Byte_Buffer{sizeof(f32) * 2 * width_x * depth_z, vtx}
							  , VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
							  , cmdpool
							  , mesh_xz_values
							  , gpu);

    Vulkan::invoke_staging_buffer_for_device_local_buffer(Memory_Byte_Buffer{sizeof(u32) * 11 * (((width_x - 1) * (depth_z - 1)) / 4), idx} // <--- this is idx, not vtx .... (stupid error)
							  , VK_BUFFER_USAGE_INDEX_BUFFER_BIT
							  , cmdpool
							  , idx_buffer
							  , gpu);

    model_info->attribute_count = 2;
    model_info->attributes_buffer = (VkVertexInputAttributeDescription *)allocate_free_list(sizeof(VkVertexInputAttributeDescription) * model_info->attribute_count);
    model_info->binding_count = 2;
    model_info->bindings = (Vulkan::Model_Binding *)allocate_free_list(sizeof(Vulkan::Model_Binding) * model_info->binding_count);
    enum :u32 {GROUND_BASE_XY_VALUES_BND = 0, HEIGHT_BND = 1, GROUND_BASE_XY_VALUES_ATT = 0, HEIGHT_ATT = 1};
    // buffer that holds only the x-z values of each vertex - the reason is so that we can create multiple terrain meshes without copying the x-z values each time
    model_info->bindings[0].binding = 0;
    model_info->bindings[0].begin_attributes_creation(model_info->attributes_buffer);
    model_info->bindings[0].push_attribute(0, VK_FORMAT_R32G32_SFLOAT, sizeof(f32) * 2);
    model_info->bindings[0].end_attributes_creation();
    // buffer contains the y-values of each mesh and the colors of each mesh
    model_info->bindings[1].binding = 1;
    model_info->bindings[1].begin_attributes_creation(model_info->attributes_buffer);
    model_info->bindings[1].push_attribute(1, VK_FORMAT_R32_SFLOAT, sizeof(f32));
    model_info->bindings[1].end_attributes_creation();

    model_info->index_data.index_type = VK_INDEX_TYPE_UINT32;
    model_info->index_data.index_offset = 0; 
    model_info->index_data.index_count = 11 * (((width_x - 1) * (depth_z - 1)) / 4);
    model_info->index_data.index_buffer = idx_buffer->buffer;
    
    pop_stack();
    pop_stack();
}

internal void
make_3D_terrain_mesh_instance(u32 width_x
			      , u32 depth_z
			      , f32 *&cpu_side_heights
			      , Vulkan::Buffer *gpu_side_heights
			      , Vulkan::Mapped_GPU_Memory *mapped_gpu_heights
			      , VkCommandPool *cmdpool
			      , Vulkan::GPU *gpu)
{
    cpu_side_heights = (f32 *)allocate_free_list(sizeof(f32) * width_x * depth_z);
    memset(cpu_side_heights, 0, sizeof(f32) * width_x * depth_z);

    // ---- testing normal calculation in the geometry shader ----
    /*    for (u32 z = 0; z < depth_z; ++z)
    {
	for (u32 x = 0; x < width_x; ++x)
	{
	    u32 index = get_terrain_index(x, z, depth_z);
	    cpu_side_heights[index] = sin((f32)x / 10.0f) * cos((f32)z / 4.0f) * 3.0f;
	}
    }*/

    Vulkan::init_buffer(sizeof(f32) * width_x * depth_z
			, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
			, VK_SHARING_MODE_EXCLUSIVE
			, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
			, gpu
			, gpu_side_heights);

    *mapped_gpu_heights = gpu_side_heights->construct_map();

    mapped_gpu_heights->begin(gpu);
    memset(mapped_gpu_heights->data, 0, sizeof(f32) * width_x * depth_z);
    //    memcpy(mapped_gpu_heights->data, cpu_side_heights, width_x * depth_z * sizeof(f32));

    mapped_gpu_heights->flush(0, sizeof(f32) * width_x * depth_z, gpu);
    
    /*Vulkan::invoke_staging_buffer_for_device_local_buffer(Memory_Byte_Buffer{sizeof(f32) * width_x * depth_z, cpu_side_heights}
							  , VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
							  , cmdpool
							  , gpu_side_heights
							  , gpu);*/
}

internal void
make_morphable_terrain_master(VkCommandPool *cmdpool
			      , Vulkan::GPU *gpu)
{
    // ---- register the info of the model for json loader to access ---
    terrain_master.model_info = register_memory("vulkan_model.terrain_base_info"_hash, sizeof(Vulkan::Model));
    
    make_3D_terrain_base(21, 21
			 , 1.0f
			 , &terrain_master.mesh_xz_values
			 , &terrain_master.idx_buffer
			 , terrain_master.model_info.p
			 , cmdpool
			 , gpu);

    make_3D_terrain_mesh_instance(21, 21
				  , terrain_master.green_mesh.heights
				  , &terrain_master.green_mesh.heights_gpu_buffer
				  , &terrain_master.green_mesh.mapped_gpu_heights
				  , cmdpool
				  , gpu);
    terrain_master.green_mesh.xz_dim = glm::ivec2(21, 21);

    make_3D_terrain_mesh_instance(21, 21
				  , terrain_master.red_mesh.heights
				  , &terrain_master.red_mesh.heights_gpu_buffer
				  , &terrain_master.red_mesh.mapped_gpu_heights
				  , cmdpool
				  , gpu);
    terrain_master.red_mesh.xz_dim = glm::ivec2(21, 21);
}

internal void
make_terrain_pointer(void)
{
    terrain_master.terrain_pointer.ppln = get_memory("pipeline.terrain_mesh_pointer_pipeline"_hash);
    terrain_master.terrain_pointer.t = &terrain_master.red_mesh;
}

internal void
prepare_terrain_pointer_for_render(VkCommandBuffer *cmdbuf
				   , VkDescriptorSet *ubo_set)
{
    // if the get_coord_pointing_at returns a coord with a negative - player is not pointing at the terrain
    if (terrain_master.terrain_pointer.ts_position.x >= 0)
    {
	Vulkan::command_buffer_bind_pipeline(terrain_master.terrain_pointer.ppln.p
					     , cmdbuf);

	Vulkan::command_buffer_bind_descriptor_sets(terrain_master.terrain_pointer.ppln.p
						    , {1, ubo_set}
						    , cmdbuf);

	struct
	{
	    glm::mat4 ts_to_ws_terrain_model;
	    glm::vec4 color;
	    glm::vec4 ts_center_position;
	    // center first
	    f32 ts_heights[8];
	} push_k;

	push_k.ts_to_ws_terrain_model = terrain_master.terrain_pointer.t->push_k.transform;
	push_k.color = glm::vec4(1.0f);
	push_k.ts_center_position = glm::vec4((f32)terrain_master.terrain_pointer.ts_position.x
					      , 0.0f
					      , (f32)terrain_master.terrain_pointer.ts_position.y
					      , 1.0f);

	u32 x = terrain_master.terrain_pointer.ts_position.x;
	u32 z = terrain_master.terrain_pointer.ts_position.y;
	u32 width = terrain_master.terrain_pointer.t->xz_dim.x;
	u32 depth = terrain_master.terrain_pointer.t->xz_dim.y;
	f32 *heights = (f32 *)(terrain_master.terrain_pointer.t->mapped_gpu_heights.data);

	auto calculate_height = [width, depth, heights](s32 x, s32 z) -> f32
	{
	    s32 i = 0;
	    if ((i = get_terrain_index(x, z, width, depth)) >= 0)
	    {
		return(heights[i]);
	    }
	    else
	    {
		return(-1.0f);
	    }
	};
	
	push_k.ts_heights[0] = calculate_height(x, z);
	push_k.ts_heights[1] = calculate_height(x - 1, z - 1);
	push_k.ts_heights[2] = calculate_height(x, z);
	push_k.ts_heights[3] = calculate_height(x + 1, z - 1);
	push_k.ts_heights[4] = calculate_height(x, z);
	push_k.ts_heights[5] = calculate_height(x + 1, z + 1);
	push_k.ts_heights[6] = calculate_height(x, z);
	push_k.ts_heights[7] = calculate_height(x - 1, z + 1);
    
	Vulkan::command_buffer_push_constant(&push_k
					     , sizeof(push_k)
					     , 0
					     , VK_SHADER_STAGE_VERTEX_BIT
					     , terrain_master.terrain_pointer.ppln.p
					     , cmdbuf);

	Vulkan::command_buffer_draw(cmdbuf
				    , 8
				    , 1
				    , 0
				    , 0);
    }
    // else don't render the pointer at all
}




#define MAX_ENTITIES 100

typedef struct Entity
{
    Entity(void) = default;
    
    enum Is_Group { IS_NOT_GROUP = false
		    , IS_GROUP = true } is_group{};

    Constant_String id {""_hash};
    // position, direction, velocity
    // in above entity group space
    glm::vec3 gs_p{0.0f}, ws_d{0.0f}, gs_v{0.0f};
    glm::quat gs_r{0.0f, 0.0f, 0.0f, 0.0f};

    // push constant stuff for the graphics pipeline
    struct
    {
	// in world space
	glm::mat4x4 ws_t{1.0f};
    } push_k;
    
    // always will be a entity group - so, to update all the groups only, will climb UP the ladder
    Entity_View above;
    Entity_View index;

    using Entity_State_Flags = u32;
    enum {GROUP_PHYSICS_INFO_HAS_BEEN_UPDATED_BIT = 1 << 0};
    Entity_State_Flags flags{};
    
    union
    {
	struct
	{
	    u32 below_count;
	    Memory_Buffer_View<Entity_View> below;
	};
    };
} Entity, Entity_Group;

Entity
construct_entity(const Constant_String &name
		 , Entity::Is_Group is_group
		 , glm::vec3 gs_p
		 , glm::vec3 ws_d
		 , glm::quat gs_r)
{
    Entity e;
    e.is_group = is_group;
    e.gs_p = gs_p;
    e.ws_d = ws_d;
    e.gs_r = gs_r;
    e.id = name;
    return(e);
}

global_var struct Entities
{
    s32 count_singles = {};
    Entity list_singles[MAX_ENTITIES] = {};

    s32 count_groups = {};
    Entity_Group list_groups[10] = {};

    Hash_Table_Inline<Entity_View, 25, 5, 5> name_map{"map.entities"};
    
    // have some sort of stack of REMOVED entities
} entities;

// contains the top entity / entity_group
global_var Entity_View scene_graph;

internal Entity *
get_entity(const Constant_String &name)
{
    Entity_View v = *entities.name_map.get(name.hash);
    return(&entities.list_singles[v.id]);
}

internal Entity_Group *
get_entity(Entity_View v)
{
    return(&entities.list_singles[v.id]);
}

internal Entity *
get_entity_group(const Constant_String &name)
{
    Entity_View v = *entities.name_map.get(name.hash);
    return(&entities.list_groups[v.id]);
}

internal Entity *
get_entity_group(Entity_Group_View v)
{
    return(&entities.list_groups[v.id]);
}

internal Entity_View
add_entity(const Entity &e
	   , Entity_Group *group_e_belongs_to)
{
    Entity_View view;
    view.id = entities.count_singles;
    view.is_group = (Entity_View::Is_Group)e.is_group;

    entities.name_map.insert(e.id.hash, view);
    
    entities.list_singles[entities.count_singles++] = e;

    auto e_ptr = get_entity(view);

    e_ptr->index = view;

    if (group_e_belongs_to)
    {
	assert(group_e_belongs_to->below_count <= group_e_belongs_to->below.count);
	
	e_ptr->above = group_e_belongs_to->index;
	group_e_belongs_to->below[group_e_belongs_to->below_count++] = e_ptr->index;
    }
    else e_ptr->above.id = -1;

    return(view);
}

internal Entity_Group_View
add_entity_group(const Entity_Group &e
		 , Entity_Group *group_e_belongs_to
		 , u32 group_below_max)
{
    Entity_Group_View view;
    view.id = entities.count_groups;
    view.is_group = (Entity_View::Is_Group)e.is_group;

    entities.name_map.insert(e.id.hash, view);
    
    entities.list_groups[entities.count_groups++] = e;

    auto e_ptr = get_entity_group(view);

    e_ptr->index = view;

    if (group_e_belongs_to)
    {
	// make sure that the capacity of the below buffer wasn't met
	assert(group_e_belongs_to->below_count <= group_e_belongs_to->below.count);
	
	e_ptr->above = group_e_belongs_to->index;
	group_e_belongs_to->below.buffer[group_e_belongs_to->below_count++] = e_ptr->index;
    }
    else e_ptr->above.id = -1;

    e_ptr->below_count = 0;
    allocate_memory_buffer(e_ptr->below, group_below_max);

    return(view);
}

internal void
init_scene_graph(void)
{
    // init first entity group (that will contain all the entities)
    // everything moves with top (aka the base entitygroup)
    Entity_Group top = construct_entity("entity.group.top"_hash
					, Entity::Is_Group::IS_GROUP
					, glm::vec3(10.0f)
					, glm::vec3(0.0f)
					, glm::quat(0.0f, 0.0f, 0.0f, 0.0f));
    
    Entity_Group_View top_view = add_entity_group(top, nullptr, MAX_ENTITIES_UNDER_TOP);
}

internal void
make_entity_renderable(Entity *e_ptr
		       , R_Mem<Vulkan::Model> model
		       , Render_Command_Recorder *recorder)
{
    // ---- adds an entity to the stack of renderables
    recorder->add(&e_ptr->push_k
		  , sizeof(e_ptr->push_k)
		  , model->raw_cache_for_rendering
		  , model->index_data
		  , Vulkan::init_draw_indexed_data_default(1, model->index_data.index_count));
}

internal void
make_entity_instanced_renderable(R_Mem<Vulkan::Model> model
				 , const Constant_String &e_mtrl_name)
{
    // TODO(luc) : first need to add support for instance rendering in material renderers.
}

// problem : rotation doesn't incorporate position
internal void
update_entity_group(Entity_Group *g)
{
    if (!(g->flags & Entity::GROUP_PHYSICS_INFO_HAS_BEEN_UPDATED_BIT))
    {
	// update position or whatever + including the position of above groups
	if (g->above.id != -1) update_entity_group(get_entity_group(g->above));

	const glm::mat4x4 *above_ws_t = (g->above.id == -1) ? &IDENTITY_MAT4X4 : &get_entity_group(g->above)->push_k.ws_t;
	g->push_k.ws_t = glm::translate(g->gs_p) * glm::mat4_cast(g->gs_r);
	g->push_k.ws_t = *above_ws_t * g->push_k.ws_t;

	g->flags |= Entity_Group::GROUP_PHYSICS_INFO_HAS_BEEN_UPDATED_BIT;
    }
    else
    {
	// do nothing
    }
}

internal void
update_scene_graph(void)
{
    // first only update entity groups
    for (s32 i = 0; i < entities.count_groups; ++i)
    {
	Entity_Group *g = &entities.list_groups[i];
	if (g->flags & Entity_Group::GROUP_PHYSICS_INFO_HAS_BEEN_UPDATED_BIT)
	{
	    continue;
	}
	else
	{
	    update_entity_group(g);
	}
    }

    for (s32 i = 0; i < entities.count_singles; ++i)
    {
	Entity *e = &entities.list_singles[i];
	e->push_k.ws_t = get_entity_group(e->above)->push_k.ws_t * glm::translate(e->gs_p) * glm::mat4_cast(e->gs_r);
    }

    for (s32 i = 0; i < entities.count_groups; ++i)
    {
	Entity_Group *g = &entities.list_groups[i];
	g->flags = 0;
	g->push_k.ws_t = glm::mat4(1.0f);
    }
}

void
Camera::set_default(f32 w, f32 h, f32 m_x, f32 m_y)
{
    mp = glm::vec2(m_x, m_y);
    p = glm::vec3(50.0f, 10.0f, 280.0f);
    d = glm::vec3(+1, 0.0f, +1);
    u = glm::vec3(0, 1, 0);

    fov = 60.0f;
    asp = w / h;
    n = 0.1f;
    f = 10000.0f;
}

void
Camera::compute_projection(void)
{
    p_m = glm::perspective(fov, asp, n, f);
}

void
Camera::compute_view(void)
{
    v_m = glm::lookAt(p, p + d, u);
}





internal void
init_atmosphere_cubemap(Vulkan::GPU *gpu)
{
    persist constexpr u32 ATMOSPHERE_CUBEMAP_IMAGE_WIDTH = 1000;
    persist constexpr u32 ATMOSPHERE_CUBEMAP_IMAGE_HEIGHT = 1000;
    
    R_Mem<Vulkan::Image2D> cubemap = register_memory("image2D.atmosphere_cubemap"_hash
								     , sizeof(Vulkan::Image2D));

    Vulkan::init_image(ATMOSPHERE_CUBEMAP_IMAGE_WIDTH
			   , ATMOSPHERE_CUBEMAP_IMAGE_HEIGHT
			   , VK_FORMAT_R8G8B8A8_UNORM
			   , VK_IMAGE_TILING_OPTIMAL
			   , VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
			   , VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
			   , 6
			   , gpu
			   , cubemap.p
			   , VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT);
 
    Vulkan::init_image_view(&cubemap->image
				, VK_FORMAT_R8G8B8A8_UNORM
				, VK_IMAGE_ASPECT_COLOR_BIT
				, gpu
				, &cubemap->image_view
				, VK_IMAGE_VIEW_TYPE_CUBE
				, 6);

    Vulkan::init_image_sampler(VK_FILTER_LINEAR
				   , VK_FILTER_LINEAR
				   , VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
				   , VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
				   , VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
				   , VK_TRUE
				   , 16
				   , VK_BORDER_COLOR_INT_OPAQUE_BLACK
				   , VK_TRUE
				   , VK_COMPARE_OP_ALWAYS
				   , VK_SAMPLER_MIPMAP_MODE_LINEAR
				   , 0.0f, 0.0f, 0.0f
				   , gpu
				   , &cubemap->image_sampler);
}

internal void
init_atmosphere_render_descriptor_set(Vulkan::GPU *gpu)
{
    R_Mem<VkDescriptorSetLayout> render_atmos_layout = get_memory("descriptor_set_layout.render_atmosphere_layout"_hash);
    R_Mem<Vulkan::Descriptor_Pool> descriptor_pool = get_memory("descriptor_pool.test_descriptor_pool"_hash);
    R_Mem<Vulkan::Image2D> cubemap_image = get_memory("image2D.atmosphere_cubemap"_hash);
    
    // just initialize the combined sampler one
    // the uniform buffer will be already created
    R_Mem<Vulkan::Descriptor_Set> cubemap_set = register_memory("descriptor_set.cubemap"_hash
								    , sizeof(Vulkan::Descriptor_Set));

    auto *p = cubemap_set.p;
    Memory_Buffer_View<Vulkan::Descriptor_Set *> sets = {1, &p};
    Vulkan::allocate_descriptor_sets(sets
				     , Memory_Buffer_View<VkDescriptorSetLayout>{1, render_atmos_layout.p}
				     , gpu
				     , &descriptor_pool.p->pool);
    
    VkDescriptorImageInfo image_info = cubemap_image.p->make_descriptor_info(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    VkWriteDescriptorSet descriptor_write = {};
    Vulkan::init_image_descriptor_set_write(cubemap_set.p, 0, 0, 1, &image_info, &descriptor_write);

    Vulkan::update_descriptor_sets(Memory_Buffer_View<VkWriteDescriptorSet>{1, &descriptor_write}
				       , gpu);
}









internal void
get_registered_objects_from_json(void)
{
    world_rendering.deferred.render_pass	= get_memory("render_pass.deferred_render_pass"_hash);
    world_rendering.atmosphere.make_render_pass = get_memory("render_pass.atmosphere_render_pass"_hash);

    world_rendering.deferred.fbos		= get_memory("framebuffer.main_fbo"_hash);
    world_rendering.atmosphere.make_fbo		= get_memory("framebuffer.atmosphere_fbo"_hash);

    world_rendering.deferred.main_pipeline	= get_memory("pipeline.main_pipeline"_hash);
    world_rendering.deferred.lighting_pipeline	= get_memory("pipeline.deferred_pipeline"_hash);
    world_rendering.atmosphere.render_pipeline	= get_memory("pipeline.render_atmosphere"_hash);
    world_rendering.atmosphere.make_pipeline	= get_memory("pipeline.atmosphere_pipeline"_hash);

    world_rendering.atmosphere.render_layout	= get_memory("descriptor_set_layout.render_atmosphere_layout"_hash);
    world_rendering.atmosphere.make_layout	= get_memory("descriptor_set_layout.atmosphere_layout"_hash);
    world_rendering.test.sets			= get_memory("descriptor_set.test_descriptor_sets"_hash);
    world_rendering.atmosphere.cubemap_set	= get_memory("descriptor_set.cubemap"_hash);
    world_rendering.deferred.descriptor_set     = get_memory("descriptor_set.deferred_descriptor_sets"_hash);

    terrain_master.terrain_ppln			= get_memory("pipeline.terrain_pipeline"_hash);
}







void
make_world(Window_Data *window
	   , Vulkan::State *vk
	   , VkCommandPool *cmdpool)
{
    prepare_external_loading_state(&vk->gpu, &vk->swapchain, cmdpool);
    make_morphable_terrain_master(cmdpool, &vk->gpu);
    
    
    world.user_camera.set_default(window->w, window->h, window->m_x, window->m_y);


    
    load_render_passes_from_json(&vk->gpu, &vk->swapchain);
    clear_linear();

    
    // initialize the cubemap that will be used as an attachment by the fbo for rendering the skybox
    init_atmosphere_cubemap(&vk->gpu);



    
    load_framebuffers_from_json(&vk->gpu, &vk->swapchain);
    clear_linear();
    
    


    
    load_descriptors_from_json(&vk->gpu, &vk->swapchain);
    clear_linear();
    
    
    init_atmosphere_render_descriptor_set(&vk->gpu);
    
    load_pipelines_from_json(&vk->gpu, &vk->swapchain);
    clear_linear();

    make_terrain_pointer();


    
    get_registered_objects_from_json();


    
    // ---- prepare the command recorders ----
    {
	world_rendering.player_recorder.init(world_rendering.recording_buffer_pool
					   , world_rendering.active_cmdbuf_count
					   , world_rendering.deferred.main_pipeline
					   , VK_SHADER_STAGE_VERTEX_BIT
					   , 10
					   , cmdpool
					   , &vk->gpu);

	world_rendering.terrain_recorder.init(world_rendering.recording_buffer_pool
					      , world_rendering.active_cmdbuf_count
					      , terrain_master.terrain_ppln
					      , VK_SHADER_STAGE_VERTEX_BIT
					      , 10
					      , cmdpool
					      , &vk->gpu);

	// ---- add test terrain to the recorder ----
	terrain_master.green_mesh.vbos[0] = terrain_master.mesh_xz_values.buffer;
	terrain_master.green_mesh.vbos[1] = terrain_master.green_mesh.heights_gpu_buffer.buffer;
	world_rendering.terrain_recorder.add(&terrain_master.green_mesh.push_k
					     , sizeof(terrain_master.green_mesh.push_k)
					     , {2, terrain_master.green_mesh.vbos}
					     , terrain_master.model_info->index_data
					     , Vulkan::init_draw_indexed_data_default(1, terrain_master.model_info->index_data.index_count));

	terrain_master.green_mesh.ws_p = glm::vec3(200.0f, 0.0f, 0.0f);
	terrain_master.green_mesh.gs_r = glm::quat(glm::radians(glm::vec3(0.0f, 45.0f, 20.0f)));
	terrain_master.green_mesh.size = glm::vec3(10.0f);
	terrain_master.green_mesh.push_k.color = glm::vec3(0.1, 0.6, 0.2) * 0.7f;
	terrain_master.green_mesh.push_k.transform =
	    glm::translate(terrain_master.green_mesh.ws_p)
	    * glm::mat4_cast(terrain_master.green_mesh.gs_r)
	    * glm::scale(terrain_master.green_mesh.size);

	terrain_master.red_mesh.vbos[0] = terrain_master.mesh_xz_values.buffer;
	terrain_master.red_mesh.vbos[1] = terrain_master.red_mesh.heights_gpu_buffer.buffer;
	world_rendering.terrain_recorder.add(&terrain_master.red_mesh.push_k
					     , sizeof(terrain_master.red_mesh.push_k)
					     , {2, terrain_master.red_mesh.vbos}
					     , terrain_master.model_info->index_data
					     , Vulkan::init_draw_indexed_data_default(1, terrain_master.model_info->index_data.index_count));

	terrain_master.red_mesh.ws_p = glm::vec3(0.0f, 0.0f, 200.0f);
	terrain_master.red_mesh.gs_r = glm::quat(glm::radians(glm::vec3(30.0f, 20.0f, 0.0f)));
	terrain_master.red_mesh.size = glm::vec3(15.0f);
	terrain_master.red_mesh.push_k.color = glm::vec3(0.6, 0.1, 0.2) * 0.7f;
	terrain_master.red_mesh.push_k.transform =
	    glm::translate(terrain_master.red_mesh.ws_p)
	    * glm::mat4_cast(terrain_master.red_mesh.gs_r)
	    * glm::scale(terrain_master.red_mesh.size);
    }
    
    
    

    
    init_scene_graph();
    // add en entity group
    Entity_Group test_g0 = construct_entity("entity.group.test0"_hash
					   , Entity::Is_Group::IS_GROUP
					   , glm::vec3(0.0f, 0.0f, 0.0f)
					   , glm::vec3(0.0f)
					   , glm::quat(0, 0, 0, 0));
    add_entity_group(test_g0
		     , get_entity_group("entity.group.top"_hash)
		     , MAX_ENTITIES_UNDER_TOP);

    // concrete representation of group test0
    Entity e = construct_entity("entity.bound_group_test0"_hash
				, Entity::Is_Group::IS_NOT_GROUP
				, glm::vec3(0.0f)
				, glm::vec3(0.0f)
				, glm::quat(0, 0, 0, 0));

    Entity_View ev = add_entity(e
				, get_entity_group("entity.group.test0"_hash));

    auto *e_ptr = get_entity(ev);

    make_entity_renderable(e_ptr
			   , get_memory("vulkan_model.test_model"_hash)
			   , &world_rendering.player_recorder);

    // create entity group that rotates around group test0
    Entity_Group rotate = construct_entity("entity.group.rotate"_hash
					   , Entity::Is_Group::IS_GROUP
					   , glm::vec3(0.0f)
					   , glm::vec3(0.0f)
					   , glm::quat(glm::vec3(0.0f))); // quat is going to change every frame

    add_entity_group(rotate
		     , get_entity_group("entity.group.test0"_hash)
		     , MAX_ENTITIES_UNDER_TOP);

    // add rotating entity
    Entity r = construct_entity("entity.rotating"_hash
				, Entity::Is_Group::IS_NOT_GROUP
				, glm::vec3(0.0f, 30.0f, 0.0f)
				, glm::vec3(0.0f)
				, glm::quat(0, 0, 0, 0));

    Entity_View rv = add_entity(r
				, get_entity_group("entity.group.rotate"_hash));

    auto *r_ptr = get_entity(rv);

    make_entity_renderable(r_ptr
			   , get_memory("vulkan_model.test_model"_hash)
			   , &world_rendering.player_recorder);

    Entity_Group rg2 = construct_entity("entity.group.rotate2"_hash
					, Entity::Is_Group::IS_GROUP
					, glm::vec3(0.0f, 0.0f, 0.0f)
					, glm::vec3(0.0f)
					, glm::quat(glm::vec3(0)));

    Entity_Group_View rg2_view = add_entity_group(rg2
						  , get_entity_group("entity.group.rotate"_hash)
						  , 5);

    Entity re2 = construct_entity("entity.rotating2"_hash
				       , Entity::Is_Group::IS_NOT_GROUP
				       , glm::vec3(0.0f, 10.0f, 0.0f)
				       , glm::vec3(0.0f)
				       , glm::quat(0, 0, 0, 0));

    Entity_View rev2 = add_entity(re2
				  , get_entity_group("entity.group.rotate2"_hash));

    auto *rev2_ptr = get_entity(rev2);
    make_entity_renderable(rev2_ptr
			   , get_memory("vulkan_model.test_model"_hash)
			   , &world_rendering.player_recorder);

    {
	// ---- initialize the skybox ----
	VkCommandBuffer cmdbuf;
	Vulkan::init_single_use_command_buffer(cmdpool, &vk->gpu, &cmdbuf);
	update_skybox(&cmdbuf);
	Vulkan::destroy_single_use_command_buffer(&cmdbuf, cmdpool, &vk->gpu);
    }
}









internal void
update_ubo(u32 current_image
	   , Vulkan::GPU *gpu
	   , Vulkan::Swapchain *swapchain
	   , Memory_Buffer_View<Vulkan::Buffer> &uniform_buffers
	   , World *world)
{
    struct Uniform_Buffer_Object
    {
	alignas(16) glm::mat4 model_matrix;
	alignas(16) glm::mat4 view_matrix;
	alignas(16) glm::mat4 projection_matrix;
    };
	
    persist auto start_time = std::chrono::high_resolution_clock::now();

    auto current_time = std::chrono::high_resolution_clock::now();
    float time = std::chrono::duration<float, std::chrono::seconds::period>(current_time - start_time).count();

    Uniform_Buffer_Object ubo = {};

    ubo.model_matrix = glm::rotate(time * glm::radians(90.0f)
				   , glm::vec3(0.0f, 0.0f, 1.0f));
    /*    ubo.view_matrix = glm::lookAt(glm::vec3(2.0f)
				  , glm::vec3(0.0f)
				  , glm::vec3(0.0f, 0.0f, 1.0f));*/
    ubo.view_matrix = world->user_camera.v_m;
    ubo.projection_matrix = glm::perspective(glm::radians(60.0f)
					     , (float)swapchain->extent.width / (float)swapchain->extent.height
					     , 0.1f
					     , 100000.0f);

    ubo.projection_matrix[1][1] *= -1;

    Vulkan::Buffer &current_ubo = uniform_buffers[current_image];

    auto map = current_ubo.construct_map();
    map.begin(gpu);
    map.fill(Memory_Byte_Buffer{sizeof(ubo), &ubo});
    map.end(gpu);
}










internal f32 angle = 0.0f;

internal void
render_frame(Vulkan::State *vulkan_state
	     , u32 image_index
	     , u32 current_frame
	     , VkCommandBuffer *cmdbuf)
{
    Memory_Buffer_View<Vulkan::Buffer> ubo_mbv = world_rendering.transforms.master_ubos.to_memory_buffer_view();
    
    update_ubo(image_index
	       , &vulkan_state->gpu
	       , &vulkan_state->swapchain
	       , ubo_mbv
	       , &world);

    render_world(vulkan_state, image_index, current_frame, cmdbuf);
    // ---- exit ----
}

void
update_world(Window_Data *window
	     , Vulkan::State *vk
	     , f32 dt
	     , u32 image_index
	     , u32 current_frame
	     , VkCommandBuffer *cmdbuf)
{
    on_which_terrain(world.user_camera.p);
    
    handle_input(window, dt, &vk->gpu);
    world.user_camera.compute_view();


    // ---- currently testing some updates on the scene graph and stuff ----    
    // rotate group
    angle += 0.15f;
    if (angle > 359.0f)
    {
	angle = 0.0f;
    }

    Entity_Group *rg = get_entity_group("entity.group.rotate"_hash);
    
    rg->gs_r = glm::quat(glm::radians(glm::vec3(angle, 0.0f, 0.0f)));

    Entity_Group *rg2 = get_entity_group("entity.group.rotate2"_hash);
    
    rg2->gs_r = glm::quat(glm::radians(glm::vec3(angle * 2.0f, angle * 1.0f, 0.0f)));

    update_scene_graph();
    

    
    // ---- actually rendering the frame ----
    render_frame(vk, image_index, current_frame, cmdbuf);
}


#include <glm/gtx/string_cast.hpp>

void
handle_input(Window_Data *window
	     , f32 dt
	     , Vulkan::GPU *gpu)
{
    if (window->m_moved)
    {
#define SENSITIVITY 15.0f
    
	glm::vec2 prev_mp = world.user_camera.mp;
	glm::vec2 curr_mp = glm::vec2(window->m_x, window->m_y);

	glm::vec3 res = world.user_camera.d;
	    
	glm::vec2 d = (curr_mp - prev_mp);

	f32 x_angle = glm::radians(-d.x) * SENSITIVITY * dt;// *elapsed;
	f32 y_angle = glm::radians(-d.y) * SENSITIVITY * dt;// *elapsed;
	res = glm::mat3(glm::rotate(x_angle, world.user_camera.u)) * res;
	glm::vec3 rotate_y = glm::cross(res, world.user_camera.u);
	res = glm::mat3(glm::rotate(y_angle, rotate_y)) * res;

	world.user_camera.d = res;
	    
	world.user_camera.mp = curr_mp;
    }

    u32 movements = 0;
    f32 accelerate = 1.0f;
    
    auto acc_v = [&movements, &accelerate](const glm::vec3 &d, glm::vec3 &dst){ ++movements; dst += d * accelerate; };

    glm::vec3 d = glm::normalize(glm::vec3(world.user_camera.d.x
					   , 0.0f
					   , world.user_camera.d.z));
    
    glm::vec3 res = {};

    if (window->key_map[GLFW_KEY_P]) std::cout << glm::to_string(world.user_camera.d) << std::endl;
    if (window->key_map[GLFW_KEY_R]) accelerate = 10.0f;
    if (window->key_map[GLFW_KEY_W]) acc_v(d, res);
    if (window->key_map[GLFW_KEY_A]) acc_v(-glm::cross(d, world.user_camera.u), res);
    if (window->key_map[GLFW_KEY_S]) acc_v(-d, res);
    if (window->key_map[GLFW_KEY_D]) acc_v(glm::cross(d, world.user_camera.u), res);
    if (window->key_map[GLFW_KEY_SPACE]) acc_v(world.user_camera.u, res);
    if (window->key_map[GLFW_KEY_LEFT_SHIFT]) acc_v(-world.user_camera.u, res);

    if (window->key_map[GLFW_KEY_UP]) light_pos += glm::vec3(0.0f, 0.0f, dt) * 5.0f;
    if (window->key_map[GLFW_KEY_LEFT]) light_pos += glm::vec3(-dt, 0.0f, 0.0f) * 5.0f;
    if (window->key_map[GLFW_KEY_RIGHT]) light_pos += glm::vec3(dt, 0.0f, 0.0f) * 5.0f;
    if (window->key_map[GLFW_KEY_DOWN]) light_pos += glm::vec3(0.0f, 0.0f, -dt) * 5.0f;

    if (movements > 0)
    {
	res = res * 15.0f * dt;

	world.user_camera.p += res;
    }



    glm::ivec2 ts_coord = get_coord_pointing_at(world.user_camera.p
						, world.user_camera.d
						, &terrain_master.red_mesh
						, dt
						, gpu);
    terrain_master.terrain_pointer.ts_position = ts_coord;
    
    // ---- modify the terrain ----
    if (window->mb_map[GLFW_MOUSE_BUTTON_RIGHT])
    {
	/*	glm::ivec2 ts_coord = get_coord_pointing_at(world.user_camera.p
						    , world.user_camera.d
						    , &terrain_master.red_mesh
						    , dt
						    , gpu);*/
	if (ts_coord.x >= 0)
	{
	    morph_terrain_at(ts_coord, &terrain_master.red_mesh, dt);
	}
    }
}
