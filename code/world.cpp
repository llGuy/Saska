#include <chrono>
#include <iostream>
#include "load.hpp"
#include "world.hpp"
#include <glm/glm.hpp>
#include <GLFW/glfw3.h>
#include <glm/gtx/transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include "graphics.hpp"

#define MAX_ENTITIES_UNDER_TOP 10
#define MAX_ENTITIES_UNDER_PLANET 150

constexpr f32 PI = 3.14159265359f;

struct Cube // to get rid of later on
{
    Uniform_Layout_Handle set_layout;
    Model_Handle model;
    GPU_Buffer_Handle model_vbo;
    GPU_Buffer_Handle model_ibo;
    Uniform_Group_Handle sets;
};

struct Camera_UBO_Transforms
{
    // ---- buffers containing view matrix and projection matrix - basically data that is common to most shaders ----
    u32 count;
    GPU_Buffer_Handle master_ubos;
} transforms;

struct Camera
{
    glm::vec2 mp;
    glm::vec3 p; // position
    glm::vec3 d; // direction
    glm::vec3 u; // up

    f32 fov;
    f32 asp; // aspect ratio
    f32 n, f; // near and far planes
    
    glm::vec4 captured_frustum_corners[8] {};
    glm::vec4 captured_shadow_corners[8] {};
    
    glm::mat4 p_m;
    glm::mat4 v_m;
    
    void
    set_default(f32 w, f32 h, f32 m_x, f32 m_y)
    {
	mp = glm::vec2(m_x, m_y);
	p = glm::vec3(50.0f, 10.0f, 280.0f);
	d = glm::vec3(+1, 0.0f, +1);
	u = glm::vec3(0, 1, 0);

	fov = glm::radians(60.0f);
	asp = w / h;
	n = 1.0f;
	f = 100000.0f;
    }
    
    void
    compute_projection(void)
    {
	p_m = glm::perspective(fov, asp, n, f);
    }
};

// should not be in this file in the future
struct Screen_GUI_Quad
{
    Pipeline_Handle quad_ppln;
};

// this belongs in the graphics.cpp module
struct Deferred_Rendering_Data
{
    Render_Pass_Handle render_pass;
    Pipeline_Handle lighting_pipeline;
    Pipeline_Handle main_pipeline;
    Uniform_Group_Handle descriptor_set;
    Framebuffer_Handle fbos;
};

global_var struct World
{

    Deferred_Rendering_Data deferred;

    static constexpr u32 MAX_CAMERAS = 10;
    u32 camera_count {0};
    Camera cameras [ MAX_CAMERAS ];

    Cube test;

    Camera_UBO_Transforms transforms;

    Screen_GUI_Quad screen_quad;

    static constexpr u32 MAX_TEST_MTRLS = 10;
    
    GPU_Material_Submission_Queue entity_render_queue;
    GPU_Material_Submission_Queue terrain_render_queue;

    struct Descriptors
    {
	Vulkan::Descriptor_Pool pool;
    } desc;

    Pipeline_Handle model_shadow_ppln;
    Pipeline_Handle terrain_shadow_ppln;
    Pipeline_Handle debug_frustum_ppln;
    
} world;

s32
push_camera(Window_Data *window)
{
    world.cameras[world.camera_count++].set_default(window->w, window->h, window->m_x, window->m_y);

    return(world.camera_count - 1);
}


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
    // ---- later on this will be a pointer (index into g_gpu_buffer_manager)
    Vulkan::Buffer heights_gpu_buffer;
    Vulkan::Mapped_GPU_Memory mapped_gpu_heights;

    VkBuffer vbos[2];

    glm::mat4 inverse_transform;
    
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
    Model_Handle model_info;

    Morphable_Terrain green_mesh;
    Morphable_Terrain red_mesh;

    Pipeline_Handle terrain_ppln;

    struct
    {
	Pipeline_Handle ppln;
	// ts_position
	glm::ivec2 ts_position{-1};
	// will not be a pointer in the future
	Morphable_Terrain *t;
    } terrain_pointer;
} terrain_master;

internal void
clean_up_terrain(Vulkan::GPU *gpu)
{
    terrain_master.mesh_xz_values.destroy(gpu);
    terrain_master.idx_buffer.destroy(gpu);
    terrain_master.green_mesh.heights_gpu_buffer.destroy(gpu);
    terrain_master.red_mesh.heights_gpu_buffer.destroy(gpu);
}

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

inline glm::mat4
compute_ts_to_ws_matrix(Morphable_Terrain *t)
{
    glm::mat4 translate = glm::translate(t->ws_p);
    glm::mat4 rotate = glm::mat4_cast(t->gs_r);
    glm::mat4 scale = glm::scale(t->size);
    return(translate * rotate * scale);
}

inline glm::vec3
transform_from_ws_to_ts(const glm::vec3 &ws_v
			, Morphable_Terrain *t)
{
    glm::vec3 ts_position = t->inverse_transform * glm::vec4(ws_v, 1.0f);

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

template <typename T> internal f32
distance_squared(const T &v)
{
    return(glm::dot(v, v));
}

internal glm::ivec2
get_coord_pointing_at(glm::vec3 ws_ray_p
		      , const glm::vec3 &ws_ray_d
		      , Morphable_Terrain *t
		      , f32 dt
		      , Vulkan::GPU *gpu)
{
    persist constexpr f32 MAX_DISTANCE = 6.0f;
    persist constexpr f32 MAX_DISTANCE_SQUARED = MAX_DISTANCE * MAX_DISTANCE;
    persist constexpr f32 STEP_SIZE = 0.3f;

    glm::mat4 ws_to_ts = t->inverse_transform;
    glm::vec3 ts_ray_p_start = glm::vec3(ws_to_ts * glm::vec4(ws_ray_p, 1.0f));
    glm::vec3 ts_ray_d = glm::normalize(glm::vec3(ws_to_ts * glm::vec4(ws_ray_d, 0.0f)));
    glm::vec3 ts_ray_diff = STEP_SIZE * ts_ray_d;

    glm::ivec2 ts_position = glm::ivec2(-1);
    
    for (glm::vec3 ts_ray_step = ts_ray_d
	     ; distance_squared(ts_ray_step) < MAX_DISTANCE_SQUARED
	     ; ts_ray_step += ts_ray_diff)
    {
	glm::vec3 ts_ray_current_p = ts_ray_step + ts_ray_p_start;

	if (ts_ray_current_p.x >= 0.0f && ts_ray_current_p.x < (f32)t->xz_dim.x + 0.000001f
	    && ts_ray_current_p.z >= 0.0f && ts_ray_current_p.z < (f32)t->xz_dim.y + 0.000001f)
	{
	    u32 x = (u32)glm::round(ts_ray_current_p.x / 2.0f) * 2;
	    u32 z = (u32)glm::round(ts_ray_current_p.z / 2.0f) * 2;

	    u32 index = get_terrain_index(x, z, t->xz_dim.y);

	    f32 *heights_ptr = (f32 *)t->mapped_gpu_heights.data;
	    if (ts_ray_current_p.y < heights_ptr[index])
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

struct Detected_Collision_Return
{
    bool detected; glm::vec3 ws_at;
};

internal Detected_Collision_Return
detect_terrain_collision(const glm::vec3 &ws_p
			 , Morphable_Terrain *t)
{
    glm::mat4 ws_to_ts = t->inverse_transform;

    glm::vec3 ts_p = glm::vec3(ws_to_ts * glm::vec4(ws_p, 1.0f));

    glm::vec2 ts_p_xz = glm::vec2(ts_p.x, ts_p.z);

    // is outside the terrain
    if (ts_p_xz.x < 0.0f || ts_p_xz.x > t->xz_dim.x
        ||  ts_p_xz.y < 0.0f || ts_p_xz.y > t->xz_dim.y)
    {
	return {false};
    }

    // position of the player on one tile (square - two triangles)
    glm::vec2 ts_position_on_tile = glm::vec2(ts_p_xz.x - glm::floor(ts_p_xz.x)
					      , ts_p_xz.y - glm::floor(ts_p_xz.y));

    // starting from (0, 0)
    glm::ivec2 ts_tile_corner_position = glm::ivec2(glm::floor(ts_p_xz));


    // wrong math probably
    auto get_height_with_offset = [&t, ts_tile_corner_position, ts_position_on_tile](const glm::vec2 &offset_a
										     , const glm::vec2 &offset_b
										     , const glm::vec2 &offset_c) -> f32
	{
	    f32 tl_x = ts_tile_corner_position.x;
	    f32 tl_z = ts_tile_corner_position.y;
	
	    u32 triangle_indices[3] =
	    {
		get_terrain_index(offset_a.x + tl_x, offset_a.y + tl_z, t->xz_dim.y)
		, get_terrain_index(offset_b.x + tl_x, offset_b.y + tl_z, t->xz_dim.y)
		, get_terrain_index(offset_c.x + tl_x, offset_c.y + tl_z, t->xz_dim.y)
	    };

	    f32 *terrain_heights = (f32 *)t->mapped_gpu_heights.data;
	    glm::vec3 a = glm::vec3(offset_a.x, terrain_heights[triangle_indices[0]], offset_a.y);
	    glm::vec3 b = glm::vec3(offset_b.x, terrain_heights[triangle_indices[1]], offset_b.y);
	    glm::vec3 c = glm::vec3(offset_c.x, terrain_heights[triangle_indices[2]], offset_c.y);

	    return(barry_centric(a, b, c, ts_position_on_tile));
	};
    
    f32 ts_height;
    
    if (ts_tile_corner_position.x % 2 == 0)
    {
	if (ts_tile_corner_position.y % 2 == 0)
	{
	    if (ts_position_on_tile.y >= ts_position_on_tile.x)
	    {
		ts_height = get_height_with_offset(glm::vec2(0.0f, 0.0f)
						   , glm::vec2(0.0f, 1.0f)
						   , glm::vec2(1.0f, 1.0f));
	    }
	    else
	    {
		ts_height = get_height_with_offset(glm::vec2(0.0f, 0.0f)
						   , glm::vec2(1.0f, 1.0f)
						   , glm::vec2(1.0f, 0.0f));
	    }
	}
	else
	{
	    if (1.0f - ts_position_on_tile.y >= ts_position_on_tile.x)
	    {
		ts_height = get_height_with_offset(glm::vec2(0.0f, 1.0f)
						   , glm::vec2(1.0f, 1.0f)
						   , glm::vec2(1.0f, 0.0f));
	    }
	    else
	    {
		ts_height = get_height_with_offset(glm::vec2(0.0f, 1.0f)
						   , glm::vec2(1.0f, 0.0f)
						   , glm::vec2(0.0f, 0.0f));
	    }
	}
    }
    else
    {
	if (ts_tile_corner_position.y % 2 == 0)
	{
	    if (1.0f - ts_position_on_tile.y >= ts_position_on_tile.x)
	    {
		ts_height = get_height_with_offset(glm::vec2(0.0f, 1.0f)
						   , glm::vec2(1.0f, 1.0f)
						   , glm::vec2(1.0f, 0.0f));
	    }
	    else
	    {
		ts_height = get_height_with_offset(glm::vec2(0.0f, 1.0f)
						   , glm::vec2(1.0f, 0.0f)
						   , glm::vec2(0.0f, 0.0f));
	    }
	}
	else
	{
	    if (ts_position_on_tile.y >= ts_position_on_tile.x)
	    {
		ts_height = get_height_with_offset(glm::vec2(0.0f, 0.0f)
						   , glm::vec2(0.0f, 1.0f)
						   , glm::vec2(1.0f, 1.0f));

		
	    }
	    else
	    {
		ts_height = get_height_with_offset(glm::vec2(0.0f, 0.0f)
						   , glm::vec2(1.0f, 1.0f)
						   , glm::vec2(1.0f, 0.0f));
	    }
	}
    }
    
    // result of the terrain collision in terrain space
    glm::vec3 ts_at (ts_p_xz.x, ts_height, ts_p_xz.y);

    glm::vec3 ws_at = glm::vec3(compute_ts_to_ws_matrix(t) * glm::vec4(ts_at, 1.0f));
    
    if (ts_p.y < 0.00000001f + ts_height)
    {
	return {true, ws_at};
    }

    return {false};
}

internal void
morph_terrain_at(const glm::ivec2 &ts_position
		 , Morphable_Terrain *t
		 , f32 morph_zone_radius
		 , f32 dt)
{
    u32 morph_quotients_outer_count = (morph_zone_radius - 1) * (morph_zone_radius - 1);
    u32 morph_quotients_inner_count = morph_zone_radius * 2 - 1;
    
    struct Morph_Point
    {
	glm::ivec2 xz;
	f32 quotient;
    } *morph_quotients_outer_cache = (Morph_Point *)allocate_linear(sizeof(Morph_Point) * morph_quotients_outer_count)
	  , *morph_quotients_inner_cache = (Morph_Point *)allocate_linear(sizeof(Morph_Point) * morph_quotients_inner_count);

    morph_quotients_outer_count = morph_quotients_inner_count = 0;
    
    // ---- one quarter of the mound + prototype the mound modifier quotients for the rest of the 3/4 mounds ----
    for (s32 z = 0; z < morph_zone_radius; ++z)
    {
	for (s32 x = 0; x < morph_zone_radius; ++x)
	{
	    glm::vec2 f_coord = glm::vec2((f32)x, (f32)z);
	    f32 squared_d = distance_squared(f_coord);
	    if (squared_d >= morph_zone_radius * morph_zone_radius
		&& abs(squared_d - morph_zone_radius * morph_zone_radius) < 0.000001f) // <---- maybe don't check if d is equal...
	    {
		break;
	    }
	    // ---- morph the terrain ----
	    s32 ts_p_x = x + ts_position.x;
	    s32 ts_p_z = z + ts_position.y;
	    
	    s32 index = get_terrain_index(ts_p_x, ts_p_z, t->xz_dim.x, t->xz_dim.y);
	    
	    f32 *p = (f32 *)t->mapped_gpu_heights.data;
	    f32 a = cos(squared_d / (morph_zone_radius * morph_zone_radius));
	    a = a * a * a;

	    if (index >= 0)
	    {
		p[index] += a * dt;
	    }

	    if (x == 0 || z == 0)
	    {
		morph_quotients_inner_cache[morph_quotients_inner_count++] = Morph_Point{glm::ivec2(x, z), a};
	    }
	    else
	    {
		morph_quotients_outer_cache[morph_quotients_outer_count++] = Morph_Point{glm::ivec2(x, z), a};
	    }
	}
    }

    // ---- do other half of the center cross ----
    for (u32 inner = 0; inner < morph_quotients_inner_count; ++inner)
    {
	s32 x = -morph_quotients_inner_cache[inner].xz.x;
	s32 z = -morph_quotients_inner_cache[inner].xz.y;

	if (x == 0 && z == 0) continue;

	// ---- morph the terrain ----
	s32 ts_p_x = x + ts_position.x;
	s32 ts_p_z = z + ts_position.y;
	    
	f32 *p = (f32 *)t->mapped_gpu_heights.data;
	
	s32 index = get_terrain_index(ts_p_x, ts_p_z, t->xz_dim.x, t->xz_dim.y);
	if (index >= 0)
	{
	    p[index] += morph_quotients_inner_cache[inner].quotient * dt;
	}
    }

    // ---- do other 3/4 of the "outer" of the mound ----
    glm::ivec2 mound_quarter_multipliers[] { glm::ivec2(+1, -1), glm::ivec2(-1, -1), glm::ivec2(-1, +1) };
    for (u32 m = 0; m < 3; ++m)
    {
	for (u32 outer = 0; outer < morph_quotients_outer_count; ++outer)
	{
	    s32 x = morph_quotients_outer_cache[outer].xz.x * mound_quarter_multipliers[m].x;
	    s32 z = morph_quotients_outer_cache[outer].xz.y * mound_quarter_multipliers[m].y;

	    // ---- morph the terrain ----
	    s32 ts_p_x = x + ts_position.x;
	    s32 ts_p_z = z + ts_position.y;
	    
	    s32 index = get_terrain_index(ts_p_x, ts_p_z, t->xz_dim.x, t->xz_dim.y);
	    f32 *p = (f32 *)t->mapped_gpu_heights.data;

	    if (index >= 0)
	    {
		p[index] += morph_quotients_outer_cache[outer].quotient * dt;
	    }
	}
    }
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
							  , VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT
							  , cmdpool
							  , mesh_xz_values
							  , gpu);

    Vulkan::invoke_staging_buffer_for_device_local_buffer(Memory_Byte_Buffer{sizeof(u32) * 11 * (((width_x - 1) * (depth_z - 1)) / 4), idx} // <--- this is idx, not vtx .... (stupid error)
							  , VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT
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
    // Don't need in the future
    cpu_side_heights = (f32 *)allocate_free_list(sizeof(f32) * width_x * depth_z);
    memset(cpu_side_heights, 0, sizeof(f32) * width_x * depth_z);

    Vulkan::init_buffer(Vulkan::adjust_memory_size_for_gpu_alignment(sizeof(f32) * width_x * depth_z, gpu)
			, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
			, VK_SHARING_MODE_EXCLUSIVE
			, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
			, gpu
			, gpu_side_heights);

    *mapped_gpu_heights = gpu_side_heights->construct_map();

    mapped_gpu_heights->begin(gpu);
    memset(mapped_gpu_heights->data, 0, Vulkan::adjust_memory_size_for_gpu_alignment(sizeof(f32) * width_x * depth_z, gpu));

    mapped_gpu_heights->flush(0, Vulkan::adjust_memory_size_for_gpu_alignment(sizeof(f32) * width_x * depth_z, gpu), gpu);
}

internal void
make_morphable_terrain_master(VkCommandPool *cmdpool
			      , Vulkan::GPU *gpu)
{
    // ---- register the info of the model for json loader to access ---
    terrain_master.model_info = g_model_manager.add("vulkan_model.terrain_base_info"_hash);
    auto *model_info = g_model_manager.get(terrain_master.model_info);
    
    make_3D_terrain_base(21, 21
			 , 1.0f
			 , &terrain_master.mesh_xz_values
			 , &terrain_master.idx_buffer
			 , model_info
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
    terrain_master.terrain_pointer.ppln = g_pipeline_manager.get_handle("pipeline.terrain_mesh_pointer_pipeline"_hash);
    terrain_master.terrain_pointer.t = &terrain_master.red_mesh;
}

internal void
prepare_terrain_pointer_for_render(GPU_Command_Queue *queue
				   , VkDescriptorSet *ubo_set)
{
    // if the get_coord_pointing_at returns a coord with a negative - player is not pointing at the terrain
    if (terrain_master.terrain_pointer.ts_position.x >= 0)
    {
	auto *ppln = g_pipeline_manager.get(terrain_master.terrain_pointer.ppln);
	Vulkan::command_buffer_bind_pipeline(ppln
					     , &queue->q);

	Vulkan::command_buffer_bind_descriptor_sets(ppln
						    , {1, ubo_set}
						    , &queue->q);

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
					     , ppln
					     , &queue->q);

	Vulkan::command_buffer_draw(&queue->q
				    , 8
				    , 1
				    , 0
				    , 0);
    }
    // else don't render the pointer at all
}

using Entity_Handle = s32;

struct Physics_Component
{
    u32 entity_index;
    
    glm::vec3 gravity_force_accumulation = {};

    bool enabled;
    
    // other forces (friction...)
};

struct Camera_Component
{
    u32 entity_index;
    
    // Can be set to -1, in that case, there is no camera bound
    s32 camera{-1};

    // Maybe some other variables to do with 3rd person / first person configs ...
};

struct Input_Component
{
    u32 entity_index;
};

struct Rendering_Component
{
    u32 entity_index;
    
    // push constant stuff for the graphics pipeline
    struct
    {
	// in world space
	glm::mat4x4 ws_t{1.0f};
	glm::vec4 color;
    } push_k;
};

struct Entity
{
    Entity(void) = default;
    
    Constant_String id {""_hash};
    // position, direction, velocity
    // in above entity group space
    glm::vec3 ws_p{0.0f}, ws_d{0.0f}, ws_v{0.0f}, ws_input_v{0.0f};
    glm::quat ws_r{0.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 size{1.0f};

    // For now is a pointer - is not a component because all entities will have one
    Morphable_Terrain *on_t;

    struct Components
    {

        s32 camera_component;
        s32 physics_component;
        s32 input_component;
        s32 rendering_component;
        
    } components;
    
    Entity_Handle index;

    // Not needed for now because there aren't any entity groups needed yet
    union
    {
	struct
	{
	    u32 below_count;
	    Memory_Buffer_View<Entity_Handle> below;
	};
    };
};

global_var struct Entities
{
    static constexpr u32 MAX_ENTITIES = 30;
    
    s32 entity_count = {};
    Entity entity_list[MAX_ENTITIES] = {};

    // All possible components: 
    s32 physics_component_count = {};
    Physics_Component physics_components[MAX_ENTITIES] = {};

    s32 camera_component_count = {};
    Camera_Component camera_components[MAX_ENTITIES] = {};

    s32 input_component_count = {};
    Input_Component input_components[MAX_ENTITIES] = {};

    s32 rendering_component_count = {};
    Rendering_Component rendering_components[MAX_ENTITIES] = {};

    Hash_Table_Inline<Entity_Handle, 30, 5, 5> name_map{"map.entities"};

    // For now:
    u32 main_entity;
    
    // have some sort of stack of REMOVED entities
} entities;

Entity
construct_entity(const Constant_String &name
		 //		 , Entity::Is_Group is_group
		 , glm::vec3 gs_p
		 , glm::vec3 ws_d
		 , glm::quat gs_r)
{
    Entity e;
    //    e.is_group = is_group;
    e.ws_p = gs_p;
    e.ws_d = ws_d;
    e.ws_r = gs_r;
    e.id = name;
    return(e);
}

internal Entity *
get_entity(const Constant_String &name)
{
    Entity_Handle v = *entities.name_map.get(name.hash);
    return(&entities.entity_list[v]);
}

internal Entity *
get_entity(Entity_Handle v)
{
    return(&entities.entity_list[v]);
}

void
attach_camera_to_entity(Entity *e
                        , s32 camera_index)
{
    
}

internal Camera_Component *
add_camera_component(Entity *e
                     , u32 camera_index)
{
    e->components.camera_component = entities.camera_component_count++;
    Camera_Component *component = &entities.camera_components[ e->components.camera_component ];
    component->entity_index = e->index;
    component->camera = camera_index;

    return(component);
}

internal void
update_camera_components(f32 dt)
{
    for (u32 i = 0; i < entities.camera_component_count; ++i)
    {
        Camera_Component *component = &entities.camera_components[ i ];
        Camera *camera = &world.cameras[ component->camera ];
        Entity *e = &entities.entity_list[ component->entity_index ];

        camera->v_m = glm::lookAt(e->ws_p + e->on_t->ws_n
                                  , e->ws_p + e->on_t->ws_n + e->ws_d
                                  , e->on_t->ws_n);

        // TODO: Don't need to calculate this every frame, just when parameters change
        camera->compute_projection();

        camera->p = e->ws_p;
        camera->d = e->ws_d;
        camera->u = e->on_t->ws_n;
    }
}

internal Rendering_Component *
add_rendering_component(Entity *e)
{
    e->components.rendering_component = entities.rendering_component_count++;
    Rendering_Component *component = &entities.rendering_components[ e->components.rendering_component ];
    component->entity_index = e->index;
    component->push_k = {};

    return(component);
}

internal void
update_rendering_component(f32 dt)
{
    for (u32 i = 0; i < entities.rendering_component_count; ++i)
    {
        Rendering_Component *component = &entities.rendering_components[ i ];
        Entity *e = &entities.entity_list[ component->entity_index ];

        if (e->on_t)
        {
            component->push_k.ws_t = glm::translate(e->ws_p) * glm::mat4_cast(e->on_t->gs_r) * glm::scale(e->size);
        }
        else
        {
            component->push_k.ws_t = glm::translate(e->ws_p) * glm::scale(e->size);
        }
    }
}

internal Physics_Component *
add_physics_component(Entity *e
                      , bool enabled)
{
    e->components.physics_component = entities.physics_component_count++;
    Physics_Component *component = &entities.physics_components[ e->components.physics_component ];
    component->entity_index = e->index;
    component->enabled = enabled;

    return(component);
}

internal void
update_physics_components(f32 dt)
{
    for (u32 i = 0; i < entities.physics_component_count; ++i)
    {
        Physics_Component *component = &entities.physics_components[ i ];
        Entity *e = &entities.entity_list[ component->entity_index ];

        if (component->enabled)
        {
            Morphable_Terrain *t = e->on_t;

            glm::vec3 gravity_d = -9.5f * t->ws_n;

            Detected_Collision_Return ret = detect_terrain_collision(e->ws_p, e->on_t);
    
            if (ret.detected)
            {
                // implement coefficient of restitution
                e->ws_v = glm::vec3(0.0f);
                gravity_d = glm::vec3(0.0f);
                e->ws_p = ret.ws_at;
            }
    
            e->ws_v += gravity_d * dt;

            e->ws_p += (e->ws_v + e->ws_input_v) * dt;
        }
        else
        {
            e->ws_p += e->ws_input_v * dt;
        }
    }
}

internal Input_Component *
add_input_component(Entity *e)
{
    e->components.input_component = entities.input_component_count++;
    Input_Component *component = &entities.input_components[ e->components.input_component ];
    component->entity_index = e->index;

    return(component);
}

// Don't even know yet if this is needed ? Only one entity will have this component - maybe just keep for consistency with the system
internal void
update_input_components(Window_Data *window
                        , f32 dt
                        , Vulkan::GPU *gpu)
{
    for (u32 i = 0; i < entities.input_component_count; ++i)
    {
        Input_Component *component = &entities.input_components[i];
        Entity *e = &entities.entity_list[component->entity_index];
        Physics_Component *e_physics = &entities.physics_components[e->components.physics_component];

        glm::vec3 up = e->on_t->ws_n;
        
        // Mouse movement
        if (window->m_moved)
        {
            // TODO: Make sensitivity configurable with a file or something, and later menu
            persist constexpr u32 SENSITIVITY = 15.0f;
    
            glm::vec2 prev_mp = glm::vec2(window->prev_m_x, window->prev_m_y);
            glm::vec2 curr_mp = glm::vec2(window->m_x, window->m_y);

            glm::vec3 res = e->ws_d;
	    
            glm::vec2 d = (curr_mp - prev_mp);

            f32 x_angle = glm::radians(-d.x) * SENSITIVITY * dt;// *elapsed;
            f32 y_angle = glm::radians(-d.y) * SENSITIVITY * dt;// *elapsed;
            res = glm::mat3(glm::rotate(x_angle, up)) * res;
            glm::vec3 rotate_y = glm::cross(res, up);
            res = glm::mat3(glm::rotate(y_angle, rotate_y)) * res;

            e->ws_d = res;
        }

        // Mouse input
        glm::ivec2 ts_coord = get_coord_pointing_at(e->ws_p
                                                    , e->ws_d
                                                    , &terrain_master.red_mesh
                                                    , dt
                                                    , gpu);
        terrain_master.terrain_pointer.ts_position = ts_coord;
    
        // ---- modify the terrain ----
        if (window->mb_map[GLFW_MOUSE_BUTTON_RIGHT])
        {
            if (ts_coord.x >= 0)
            {
                morph_terrain_at(ts_coord, &terrain_master.red_mesh, 3, dt);
            }
        }

        // Keyboard input for entity
        u32 movements = 0;
        f32 accelerate = 1.0f;
    
        auto acc_v = [&movements, &accelerate](const glm::vec3 &d, glm::vec3 &dst){ ++movements; dst += d * accelerate; };

        glm::vec3 d = glm::normalize(glm::vec3(e->ws_d.x
                                               , e->ws_d.y
                                               , e->ws_d.z));

        Morphable_Terrain *t = e->on_t;
        glm::mat4 inverse = t->inverse_transform;
    
        glm::vec3 ts_d = inverse * glm::vec4(d, 0.0f);
    
        ts_d.y = 0.0f;

        d = glm::vec3(t->push_k.transform * glm::vec4(ts_d, 0.0f));
        d = glm::normalize(d);
    
        glm::vec3 res = {};

        bool detected_collision = detect_terrain_collision(e->ws_p, e->on_t).detected;
    
        if (detected_collision) e->ws_v = glm::vec3(0.0f);
    
        //    if (window->key_map[GLFW_KEY_P]) std::cout << glm::to_string(world.user_camera.d) << std::endl;
        if (window->key_map[GLFW_KEY_R]) accelerate = 10.0f;
        if (window->key_map[GLFW_KEY_W]) acc_v(d, res);
        if (window->key_map[GLFW_KEY_A]) acc_v(-glm::cross(d, up), res);
        if (window->key_map[GLFW_KEY_S]) acc_v(-d, res);
        if (window->key_map[GLFW_KEY_D]) acc_v(glm::cross(d, up), res);
    
        if (window->key_map[GLFW_KEY_SPACE])
        {
            if (e_physics->enabled)
            {
                if (detected_collision)
                {
                    // give some velotity towards the up vector
                    e->ws_v += up * 5.0f;
                    e->ws_p += e->ws_v * dt;
                }
            }
            else
            {
                acc_v(up, res);
            }
        }
    
        if (window->key_map[GLFW_KEY_LEFT_SHIFT]) acc_v(-up, res);

        if (movements > 0)
        {
            res = res * 15.0f;

            e->ws_input_v = res;
        }
        else
        {
            e->ws_input_v = glm::vec3(0.0f);
        }
    }
}

internal Entity_Handle
add_entity(const Entity &e)

{
    Entity_Handle view;
    view = entities.entity_count;

    entities.name_map.insert(e.id.hash, view);
    
    entities.entity_list[entities.entity_count++] = e;

    auto e_ptr = get_entity(view);

    e_ptr->index = view;

    return(view);
}

internal void
push_entity_to_queue(Entity *e_ptr // Needs a rendering component attached
                     , Model_Handle model_handle
                     , GPU_Material_Submission_Queue *queue)
{
    // ---- adds an entity to the stack of renderables
    auto *model = g_model_manager.get(model_handle);

    Rendering_Component *component = &entities.rendering_components[ e_ptr->components.rendering_component ];
    
    queue->push_material(&component->push_k
			 , sizeof(component->push_k)
			 , model->raw_cache_for_rendering
			 , model->index_data
			 , Vulkan::init_draw_indexed_data_default(1, model->index_data.index_count));
}

internal void
make_entity_instanced_renderable(Model_Handle model_handle
				 , const Constant_String &e_mtrl_name)
{
    // TODO(luc) : first need to add support for instance rendering in material renderers.
}

internal void
update_entities(Window_Data *window
                , f32 dt
                , Vulkan::GPU *gpu)
{
    update_input_components(window, dt, gpu);
    update_physics_components(dt);
    update_camera_components(dt);
    update_rendering_component(dt);
}




internal void
prepare_external_loading_state(Vulkan::GPU *gpu, Vulkan::Swapchain *swapchain, VkCommandPool *cmdpool)
{
    world.deferred.render_pass = g_render_pass_manager.add("render_pass.deferred_render_pass"_hash);

    // ---- make cube model info ----
    {
	world.test.model = g_model_manager.add("vulkan_model.test_model"_hash);
	auto *test_model = g_model_manager.get(world.test.model);
	
	test_model->attribute_count = 3;
	test_model->attributes_buffer = (VkVertexInputAttributeDescription *)allocate_free_list(sizeof(VkVertexInputAttributeDescription) * 3);
	test_model->binding_count = 1;
	test_model->bindings = (Vulkan::Model_Binding *)allocate_free_list(sizeof(Vulkan::Model_Binding));

	struct Vertex { glm::vec3 pos; glm::vec3 color; glm::vec2 uvs; };
	
	// only one binding
	Vulkan::Model_Binding *binding = test_model->bindings;
	binding->begin_attributes_creation(test_model->attributes_buffer);

	binding->push_attribute(0, VK_FORMAT_R32G32B32_SFLOAT, sizeof(Vertex::pos));
	binding->push_attribute(1, VK_FORMAT_R32G32B32_SFLOAT, sizeof(Vertex::color));
	binding->push_attribute(2, VK_FORMAT_R32G32_SFLOAT, sizeof(Vertex::uvs));

	binding->end_attributes_creation();
    }
    
    // ---- make descriptor set layout for rendering the cubes ----
    {
	world.test.set_layout = g_uniform_layout_manager.add("descriptor_set_layout.test_descriptor_set_layout"_hash);
	auto *layout = g_uniform_layout_manager.get(world.test.set_layout);
	
	VkDescriptorSetLayoutBinding bindings[] =
	    {
		Vulkan::init_descriptor_set_layout_binding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_GEOMETRY_BIT)
	    };
	
	Vulkan::init_descriptor_set_layout(Memory_Buffer_View<VkDescriptorSetLayoutBinding>{1, bindings}
					       , gpu
					       , layout);
    }

    // ---- make cube vbo ----
    {
	world.test.model_vbo = g_gpu_buffer_manager.add("vbo.test_model_vbo"_hash);
	auto *vbo = g_gpu_buffer_manager.get(world.test.model_vbo);
	auto *test_model = g_model_manager.get(world.test.model);
	
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
	
	auto *main_binding = &test_model->bindings[0];
	    
	Memory_Byte_Buffer byte_buffer{sizeof(vertices), vertices};
	
	Vulkan::invoke_staging_buffer_for_device_local_buffer(byte_buffer
							      , VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT
							      , cmdpool
							      , vbo
							      , gpu);

	main_binding->buffer = vbo->buffer;
	test_model->create_vbo_list();
    }

    // ---- make cube ibo ----
    {
	world.test.model_ibo = g_gpu_buffer_manager.add("ibo.test_model_ibo"_hash);
	auto *ibo = g_gpu_buffer_manager.get(world.test.model_ibo);
	auto *test_model = g_model_manager.get(world.test.model);
	
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

	test_model->index_data.index_type = VK_INDEX_TYPE_UINT32;
	test_model->index_data.index_offset = 0;
	test_model->index_data.index_count = sizeof(mesh_indices) / sizeof(mesh_indices[0]);

	Memory_Byte_Buffer byte_buffer{sizeof(mesh_indices), mesh_indices};
	    
	Vulkan::invoke_staging_buffer_for_device_local_buffer(byte_buffer
							      , VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT
							      , cmdpool
							      , ibo
							      , gpu);

	test_model->index_data.index_buffer = ibo->buffer;
    }

    // ---- make descriptor pool ----
    {
	VkDescriptorPoolSize pool_sizes[3] = {};

	Vulkan::init_descriptor_pool_size(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, swapchain->imgs.count + 5, &pool_sizes[0]);
	Vulkan::init_descriptor_pool_size(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, swapchain->imgs.count + 5, &pool_sizes[1]);
	Vulkan::init_descriptor_pool_size(VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 5, &pool_sizes[2]);

    
	Vulkan::init_descriptor_pool(Memory_Buffer_View<VkDescriptorPoolSize>{3, pool_sizes}, swapchain->imgs.count + 10, gpu, &world.desc.pool);
    }

    // ---- make the ubos ----
    {
	struct Uniform_Buffer_Object
	{
	    alignas(16) glm::mat4 model_matrix;
	    alignas(16) glm::mat4 view_matrix;
	    alignas(16) glm::mat4 projection_matrix;
	    
	    alignas(16) glm::mat4 shadow_projection_matrix;
	    alignas(16) glm::mat4 shadow_view_matrix;

	    alignas(16) glm::mat4 shadow_map_bias;

	    bool render_to_shadow;
	};
	
	u32 uniform_buffer_count = swapchain->imgs.count;

	world.transforms.count = uniform_buffer_count;
	world.transforms.master_ubos = g_gpu_buffer_manager.add("buffer.ubos"_hash, uniform_buffer_count);
	auto *ubos = g_gpu_buffer_manager.get(world.transforms.master_ubos);
	
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
				, &ubos[i]);
	}
    }
}

// ---- to orgnise later on ----
global_var glm::vec3 light_pos = glm::vec3(0.00000001f, 10.0f, 0.00000001f);

// ---- rendering of the entire world happens here ----
internal void
prepare_terrain_pointer_for_render(VkCommandBuffer *cmdbuf, VkDescriptorSet *set, Vulkan::Framebuffer *fbo);

internal void
calculate_ws_frustum_corners(glm::vec4 *corners
			     , glm::vec4 *shadow_corners)
{
    Shadow_Matrices shadow_data = get_shadow_matrices();

    Entity *main = &entities.entity_list[entities.main_entity];
    Camera_Component *camera_component = &entities.camera_components[main->components.camera_component];
    Camera *camera = &world.cameras[camera_component->camera];
    
    corners[0] = shadow_data.inverse_light_view * camera->captured_frustum_corners[0];
    corners[1] = shadow_data.inverse_light_view * camera->captured_frustum_corners[1];
    corners[3] = shadow_data.inverse_light_view * camera->captured_frustum_corners[2];
    corners[2] = shadow_data.inverse_light_view * camera->captured_frustum_corners[3];
    
    corners[4] = shadow_data.inverse_light_view * camera->captured_frustum_corners[4];
    corners[5] = shadow_data.inverse_light_view * camera->captured_frustum_corners[5];
    corners[7] = shadow_data.inverse_light_view * camera->captured_frustum_corners[6];
    corners[6] = shadow_data.inverse_light_view * camera->captured_frustum_corners[7];

    shadow_corners[0] = shadow_data.inverse_light_view * camera->captured_shadow_corners[0];
    shadow_corners[1] = shadow_data.inverse_light_view * camera->captured_shadow_corners[1];
    shadow_corners[2] = shadow_data.inverse_light_view * camera->captured_shadow_corners[2];
    shadow_corners[3] = shadow_data.inverse_light_view * camera->captured_shadow_corners[3];
    
    shadow_corners[4] = shadow_data.inverse_light_view * camera->captured_shadow_corners[4];
    shadow_corners[5] = shadow_data.inverse_light_view * camera->captured_shadow_corners[5];
    shadow_corners[6] = shadow_data.inverse_light_view * camera->captured_shadow_corners[6];
    shadow_corners[7] = shadow_data.inverse_light_view * camera->captured_shadow_corners[7];
}

internal void
render_debug_frustum(GPU_Command_Queue *queue
                     , VkDescriptorSet ubo)
{
    auto *debug_frustum_ppln = g_pipeline_manager.get(world.debug_frustum_ppln);
    Vulkan::command_buffer_bind_pipeline(debug_frustum_ppln, &queue->q);

    Vulkan::command_buffer_bind_descriptor_sets(debug_frustum_ppln
						, {1, &ubo}
						, &queue->q);

    struct Push_K
    {
	alignas(16) glm::vec4 positions[8];
	alignas(16) glm::vec4 color;
    } push_k1, push_k2;

    calculate_ws_frustum_corners(push_k1.positions, push_k2.positions);

    push_k1.color = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
    push_k2.color = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
    
    Vulkan::command_buffer_push_constant(&push_k1
					 , sizeof(push_k1)
					 , 0
					 , VK_SHADER_STAGE_VERTEX_BIT
					 , debug_frustum_ppln
					 , &queue->q);
    
    Vulkan::command_buffer_draw(&queue->q, 24, 1, 0, 0);

    Vulkan::command_buffer_push_constant(&push_k2
					 , sizeof(push_k2)
					 , 0
					 , VK_SHADER_STAGE_VERTEX_BIT
					 , debug_frustum_ppln
					 , &queue->q);
    
    Vulkan::command_buffer_draw(&queue->q, 24, 1, 0, 0);
}

internal void
draw_2D_quad_debug(VkCommandBuffer *cmdbuf
                   , Uniform_Group *shadow_map_uniform_group)
{
    struct Screen_Quad
    {
	glm::vec2 scale;
	glm::vec2 position;
    } screen_quad_push_k;

    screen_quad_push_k.scale = glm::vec2(0.2f);
    screen_quad_push_k.position = glm::vec2(-0.8f, +0.5f);

    auto *quad_ppln = g_pipeline_manager.get(world.screen_quad.quad_ppln);
    
    Vulkan::command_buffer_bind_pipeline(quad_ppln, cmdbuf);

    VkDescriptorSet quad_set[] = {*shadow_map_uniform_group};
    Vulkan::command_buffer_bind_descriptor_sets(quad_ppln
						, {1, quad_set}
						, cmdbuf);

    Vulkan::command_buffer_push_constant(&screen_quad_push_k
					 , sizeof(screen_quad_push_k)
					 , 0
					 , VK_SHADER_STAGE_VERTEX_BIT
					 , quad_ppln
					 , cmdbuf);

    Vulkan::command_buffer_draw(cmdbuf
                                , 4, 1, 0, 0);
}

internal void
render_world(Vulkan::State *vk
	     , u32 image_index
	     , u32 current_frame
	     , Memory_Buffer_View<Vulkan::Buffer> &ubos
	     , VkCommandBuffer *cmdbuf)
{
    // Fetch some data needed to render
    auto *transforms_ubo_uniform_groups = g_uniform_group_manager.get(world.test.sets);
    Shadow_Display shadow_display_data = get_shadow_display();
    
    Uniform_Group uniform_groups[2] = {transforms_ubo_uniform_groups[image_index], shadow_display_data.texture};

    Entity *e = &entities.entity_list[entities.main_entity];
    Camera_Component *e_camera_component = &entities.camera_components[e->components.camera_component];
    Camera *camera = &world.cameras[e_camera_component->camera];
    
    // Start the rendering    
    Vulkan::begin_command_buffer(cmdbuf, 0, nullptr);
    
    GPU_Command_Queue queue {*cmdbuf};

    // Rendering to the shadow map
    begin_shadow_offscreen(4000, 4000, &queue);
    {
        auto *model_ppln = g_pipeline_manager.get(world.model_shadow_ppln);

        world.entity_render_queue.submit_queued_materials({1, &transforms_ubo_uniform_groups[image_index]}, model_ppln
                                                          , &queue
                                                          , VK_COMMAND_BUFFER_LEVEL_PRIMARY);

        auto *terrain_ppln = g_pipeline_manager.get(world.terrain_shadow_ppln);    
    
        world.terrain_render_queue.submit_queued_materials({1, &transforms_ubo_uniform_groups[image_index]}, terrain_ppln
                                                           , &queue
                                                           , VK_COMMAND_BUFFER_LEVEL_PRIMARY);
    }
    end_shadow_offscreen(&queue);

    // Rendering the scene with lighting and everything
    begin_deferred_rendering(image_index, Vulkan::init_render_area({0, 0}, vk->swapchain.extent), &queue);
    {
        auto *terrain_ppln = g_pipeline_manager.get(terrain_master.terrain_ppln);    
        auto *entity_ppln = g_pipeline_manager.get(world.deferred.main_pipeline);
    
        world.terrain_render_queue.submit_queued_materials({2, uniform_groups}, terrain_ppln, &queue, VK_COMMAND_BUFFER_LEVEL_PRIMARY);
        world.entity_render_queue.submit_queued_materials({2, uniform_groups}, entity_ppln, &queue, VK_COMMAND_BUFFER_LEVEL_PRIMARY);

        prepare_terrain_pointer_for_render(&queue, &transforms_ubo_uniform_groups[image_index]);
        render_debug_frustum(&queue, transforms_ubo_uniform_groups[image_index]);
        
        // ---- render skybox ----
        render_atmosphere({1, uniform_groups}, camera->p, g_model_manager.get(world.test.model), &queue);
    }
    end_deferred_rendering(camera->v_m, &queue);

    Vulkan::end_command_buffer(cmdbuf);
}
















internal void
get_registered_objects_from_json(void)
{
    world.deferred.render_pass		= g_render_pass_manager.get_handle("render_pass.deferred_render_pass"_hash);

    world.deferred.fbos			= g_framebuffer_manager.get_handle("framebuffer.main_fbo"_hash);

    world.deferred.main_pipeline	= g_pipeline_manager.get_handle("pipeline.main_pipeline"_hash);
    world.deferred.lighting_pipeline	= g_pipeline_manager.get_handle("pipeline.deferred_pipeline"_hash);
    
    world.test.sets			= g_uniform_group_manager.get_handle("descriptor_set.test_descriptor_sets"_hash);
    world.deferred.descriptor_set	= g_uniform_group_manager.get_handle("descriptor_set.deferred_descriptor_sets"_hash);

    terrain_master.terrain_ppln		= g_pipeline_manager.get_handle("pipeline.terrain_pipeline"_hash);

    world.screen_quad.quad_ppln		= g_pipeline_manager.get_handle("pipeline.screen_quad"_hash);

    world.debug_frustum_ppln	= g_pipeline_manager.get_handle("pipeline.debug_frustum"_hash);

    world.model_shadow_ppln = g_pipeline_manager.get_handle("pipeline.model_shadow"_hash);
    world.terrain_shadow_ppln = g_pipeline_manager.get_handle("pipeline.terrain_shadow"_hash);
}







void
make_world(Window_Data *window
	   , Vulkan::State *vk
	   , VkCommandPool *cmdpool)
{
    prepare_external_loading_state(&vk->gpu, &vk->swapchain, cmdpool);
    make_morphable_terrain_master(cmdpool, &vk->gpu);
    
    std::cout << "JSON > loading render passes" << std::endl;
    load_render_passes_from_json(&vk->gpu, &vk->swapchain);
    clear_linear();

    std::cout << "JSON > loading framebuffers" << std::endl;
    load_framebuffers_from_json(&vk->gpu, &vk->swapchain);
    clear_linear();
    
    std::cout << "JSON > loading descriptors" << std::endl;
    load_descriptors_from_json(&vk->gpu, &vk->swapchain, &world.desc.pool.pool);
    clear_linear();
    
    std::cout << "JSON > loading pipelines" << std::endl;
    load_pipelines_from_json(&vk->gpu, &vk->swapchain);
    clear_linear();

    make_terrain_pointer();
    
    get_registered_objects_from_json();

    make_rendering_pipeline_data(&vk->gpu, &world.desc.pool.pool, cmdpool);


    
    // ---- prepare the command recorders ----
    {
	auto *model_info = g_model_manager.get(terrain_master.model_info);
	
	world.entity_render_queue = make_gpu_material_submission_queue(20
								       , VK_SHADER_STAGE_VERTEX_BIT
								       , VK_COMMAND_BUFFER_LEVEL_SECONDARY
								       , cmdpool
								       , &vk->gpu);

	world.terrain_render_queue = make_gpu_material_submission_queue(10
									, VK_SHADER_STAGE_VERTEX_BIT
									, VK_COMMAND_BUFFER_LEVEL_SECONDARY
									, cmdpool
									, &vk->gpu);

	// ---- add test terrain to the recorder ----
	terrain_master.green_mesh.vbos[0] = terrain_master.mesh_xz_values.buffer;
	terrain_master.green_mesh.vbos[1] = terrain_master.green_mesh.heights_gpu_buffer.buffer;
	world.terrain_render_queue.push_material(&terrain_master.green_mesh.push_k
                                                 , sizeof(terrain_master.green_mesh.push_k)
                                                 , {2, terrain_master.green_mesh.vbos}
                                                 , model_info->index_data
                                                 , Vulkan::init_draw_indexed_data_default(1, model_info->index_data.index_count));

	terrain_master.green_mesh.ws_p = glm::vec3(200.0f, 0.0f, 0.0f);
	terrain_master.green_mesh.gs_r = glm::quat(glm::radians(glm::vec3(0.0f, 45.0f, 20.0f)));
	terrain_master.green_mesh.size = glm::vec3(10.0f);
	terrain_master.green_mesh.push_k.color = glm::vec3(0.1, 0.6, 0.2) * 0.7f;
	terrain_master.green_mesh.push_k.transform =
	    glm::translate(terrain_master.green_mesh.ws_p)
	    * glm::mat4_cast(terrain_master.green_mesh.gs_r)
	    * glm::scale(terrain_master.green_mesh.size);
	terrain_master.green_mesh.inverse_transform = compute_ws_to_ts_matrix(&terrain_master.green_mesh);

	terrain_master.red_mesh.vbos[0] = terrain_master.mesh_xz_values.buffer;
	terrain_master.red_mesh.vbos[1] = terrain_master.red_mesh.heights_gpu_buffer.buffer;
	world.terrain_render_queue.push_material(&terrain_master.red_mesh.push_k
                                                 , sizeof(terrain_master.red_mesh.push_k)
                                                 , {2, terrain_master.red_mesh.vbos}
                                                 , model_info->index_data
                                                 , Vulkan::init_draw_indexed_data_default(1, model_info->index_data.index_count));

	terrain_master.red_mesh.ws_p = glm::vec3(0.0f, 0.0f, 200.0f);
	terrain_master.red_mesh.gs_r = glm::quat(glm::radians(glm::vec3(30.0f, 20.0f, 0.0f)));
	terrain_master.red_mesh.size = glm::vec3(15.0f);
	terrain_master.red_mesh.push_k.color = glm::vec3(255.0f, 69.0f, 0.0f) / 256.0f;
	terrain_master.red_mesh.push_k.transform =
	    glm::translate(terrain_master.red_mesh.ws_p)
	    * glm::mat4_cast(terrain_master.red_mesh.gs_r)
	    * glm::scale(terrain_master.red_mesh.size);
	terrain_master.red_mesh.inverse_transform = compute_ws_to_ts_matrix(&terrain_master.red_mesh);

	terrain_master.red_mesh.ws_n = glm::vec3(glm::mat4_cast(terrain_master.red_mesh.gs_r) * glm::vec4(0.0f, 1.0f, 0.0f, 0.0f));
    }
    
    
    
    Entity e = construct_entity("entity.bound_group_test0"_hash
				, glm::vec3(50.0f, 10.0f, 280.0f)
				, glm::normalize(glm::vec3(1.0f, 0.0f, 1.0f))
				, glm::quat(0, 0, 0, 0));

    Entity_Handle ev = add_entity(e);
    auto *e_ptr = get_entity(ev);
    entities.main_entity = ev;

    e_ptr->on_t = &terrain_master.red_mesh;

    add_physics_component(e_ptr, false);    
    add_camera_component(e_ptr, push_camera(window));
    add_input_component(e_ptr);
        
    // add rotating entity
    Entity r = construct_entity("entity.rotating"_hash
				, glm::vec3(200.0f, -40.0f, 300.0f)
				, glm::vec3(0.0f)
				, glm::quat(glm::radians(45.0f), glm::vec3(0.0f, 1.0f, 0.0f)));

    r.size = glm::vec3(10.0f);

    Entity_Handle rv = add_entity(r);
    auto *r_ptr = get_entity(rv);
    
    r_ptr->on_t = &terrain_master.red_mesh;

    Rendering_Component *r_ptr_rendering = add_rendering_component(r_ptr);
    add_physics_component(r_ptr, true);
    
    r_ptr_rendering->push_k.color = glm::vec4(0.2f, 0.2f, 0.8f, 1.0f);
    
    push_entity_to_queue(r_ptr
                         , g_model_manager.get_handle("vulkan_model.test_model"_hash)
                         , &world.entity_render_queue);

    Entity r2 = construct_entity("entity.rotating2"_hash
				 , glm::vec3(250.0f, -40.0f, 350.0f)
				 , glm::vec3(0.0f)
				 , glm::quat(glm::radians(45.0f), glm::vec3(0.0f, 1.0f, 0.0f)));

    r2.size = glm::vec3(10.0f);
    Entity_Handle rv2 = add_entity(r2);
    auto *r2_ptr = get_entity(rv2);

    Rendering_Component *r2_ptr_rendering = add_rendering_component(r2_ptr);
    add_physics_component(r2_ptr, false);
    
    r2_ptr_rendering->push_k.color = glm::vec4(0.6f, 0.0f, 0.6f, 1.0f);
    r2_ptr->on_t = &terrain_master.red_mesh;

    push_entity_to_queue(r2_ptr
                         , g_model_manager.get_handle("vulkan_model.test_model"_hash)
                         , &world.entity_render_queue);
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

	alignas(16) glm::mat4 shadow_projection_matrix;
	alignas(16) glm::mat4 shadow_view_matrix;

	alignas(16) glm::mat4 shadow_map_bias;

	bool render_to_shadow;
    };

    Entity *e_ptr = &entities.entity_list[entities.main_entity];
    Camera_Component *camera_component = &entities.camera_components[ e_ptr->components.camera_component ];
    Camera *camera = &world->cameras[camera_component->camera];
    
    update_shadows(1000.0f
                   , 1.0f
                   , glm::radians(60.0f)
                   , (float)swapchain->extent.width / (float)swapchain->extent.height
                   , e_ptr->ws_p
                   , e_ptr->ws_d
                   , e_ptr->on_t->ws_n);

    Shadow_Matrices shadow_data = get_shadow_matrices();

    persist auto start_time = std::chrono::high_resolution_clock::now();

    auto current_time = std::chrono::high_resolution_clock::now();
    float time = std::chrono::duration<float, std::chrono::seconds::period>(current_time - start_time).count();

    Uniform_Buffer_Object ubo = {};

    ubo.view_matrix = camera->v_m;

    ubo.projection_matrix = camera->p_m;

    ubo.projection_matrix[1][1] *= -1;

    ubo.shadow_projection_matrix = shadow_data.projection_matrix;
    ubo.shadow_view_matrix = shadow_data.light_view_matrix;

    ubo.render_to_shadow = false;

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
    
    auto *ubos = g_gpu_buffer_manager.get(world.transforms.master_ubos);
    Memory_Buffer_View<Vulkan::Buffer> ubo_mbv { world.transforms.count, ubos };

    update_ubo(image_index
	       , &vulkan_state->gpu
	       , &vulkan_state->swapchain
	       , ubo_mbv
	       , &world);

    render_world(vulkan_state, image_index, current_frame, ubo_mbv, cmdbuf);
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
    auto *e = &entities.entity_list[entities.main_entity];
    
    handle_input_debug(window, dt, &vk->gpu);
    
    update_entities(window, dt, &vk->gpu);
    
    // ---- actually rendering the frame ----
    render_frame(vk, image_index, current_frame, cmdbuf);
}


#include <glm/gtx/string_cast.hpp>


// Not to do with moving the entity, just debug stuff : will be used later for stuff like opening menus
void
handle_input_debug(Window_Data *window
                   , f32 dt
                   , Vulkan::GPU *gpu)
{
    // ---- get bound entity ----
    // TODO make sure to check if main_entity < 0
    Entity *e_ptr = &entities.entity_list[entities.main_entity];
    Camera_Component *e_camera_component = &entities.camera_components[e_ptr->components.camera_component];
    Physics_Component *e_physics = &entities.physics_components[e_ptr->components.physics_component];
    Camera *e_camera = &world.cameras[e_camera_component->camera];
    glm::vec3 up = e_ptr->on_t->ws_n;
    
    if (window->key_map[GLFW_KEY_UP]) light_pos += glm::vec3(0.0f, 0.0f, dt) * 5.0f;
    if (window->key_map[GLFW_KEY_LEFT]) light_pos += glm::vec3(-dt, 0.0f, 0.0f) * 5.0f;
    if (window->key_map[GLFW_KEY_RIGHT]) light_pos += glm::vec3(dt, 0.0f, 0.0f) * 5.0f;
    if (window->key_map[GLFW_KEY_DOWN]) light_pos += glm::vec3(0.0f, 0.0f, -dt) * 5.0f;

    Shadow_Matrices shadow_data = get_shadow_matrices();
    Shadow_Debug    shadow_debug = get_shadow_debug();
    
    shadow_data.light_view_matrix = glm::lookAt(glm::vec3(0.0f), -glm::normalize(light_pos), glm::vec3(0.0f, 1.0f, 0.0f));

    if (window->key_map[GLFW_KEY_C])
    {
	for (u32 i = 0; i < 8; ++i)
	{
	    e_camera->captured_frustum_corners[i] = shadow_debug.frustum_corners[i];
	}

	e_camera->captured_shadow_corners[0] = glm::vec4(shadow_debug.x_min, shadow_debug.y_max, shadow_debug.z_min, 1.0f);
	e_camera->captured_shadow_corners[1] = glm::vec4(shadow_debug.x_max, shadow_debug.y_max, shadow_debug.z_min, 1.0f);
	e_camera->captured_shadow_corners[2] = glm::vec4(shadow_debug.x_max, shadow_debug.y_min, shadow_debug.z_min, 1.0f);
	e_camera->captured_shadow_corners[3] = glm::vec4(shadow_debug.x_min, shadow_debug.y_min, shadow_debug.z_min, 1.0f);

	e_camera->captured_shadow_corners[4] = glm::vec4(shadow_debug.x_min, shadow_debug.y_max, shadow_debug.z_max, 1.0f);
	e_camera->captured_shadow_corners[5] = glm::vec4(shadow_debug.x_max, shadow_debug.y_max, shadow_debug.z_max, 1.0f);
	e_camera->captured_shadow_corners[6] = glm::vec4(shadow_debug.x_max, shadow_debug.y_min, shadow_debug.z_max, 1.0f);
	e_camera->captured_shadow_corners[7] = glm::vec4(shadow_debug.x_min, shadow_debug.y_min, shadow_debug.z_max, 1.0f);
    }
}



void
destroy_world(Vulkan::GPU *gpu)
{
    g_render_pass_manager.clean_up(gpu);
    g_image_manager.clean_up(gpu);
    g_framebuffer_manager.clean_up(gpu);
    g_pipeline_manager.clean_up(gpu);
    g_gpu_buffer_manager.clean_up(gpu);

    clean_up_terrain(gpu);

    for (u32 i = 0; i < g_uniform_layout_manager.count; ++i)
    {
	vkDestroyDescriptorSetLayout(gpu->logical_device, g_uniform_layout_manager.objects[i], nullptr);
    }

    vkDestroyDescriptorPool(gpu->logical_device, world.desc.pool.pool, nullptr);
}
