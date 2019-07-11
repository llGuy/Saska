#include <chrono>
#include <iostream>
#include "world.hpp"
#include <glm/glm.hpp>
#include <GLFW/glfw3.h>
#include <glm/gtx/transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include "graphics.hpp"

#define MAX_ENTITIES_UNDER_TOP 10
#define MAX_ENTITIES_UNDER_PLANET 150

constexpr f32 PI = 3.14159265359f;

global_var constexpr u32 MAX_MTRLS = 10;
global_var GPU_Material_Submission_Queue g_world_submission_queues[MAX_MTRLS];

enum { ENTITY_QUEUE, TERRAIN_QUEUE };

// ---- terrain code ----
struct Morphable_Terrain
{
    bool is_modified = false;
    
    iv2 xz_dim;
    // ---- up vector of the terrain ----
    v3 ws_n;
    f32 *heights;

    v3 size;
    v3 ws_p;
    q4 gs_r;

    u32 offset_into_heights_gpu_buffer;
    // ---- later on this will be a pointer (index into g_gpu_buffer_manager)
    GPU_Buffer heights_gpu_buffer;

    VkBuffer vbos[2];

    m4x4 inverse_transform;
    
    struct Push_K
    {
        m4x4 transform;
        v3 color;
    } push_k;
};

struct Planet
{
    // planet has 6 faces
    Morphable_Terrain meshes[6];

    v3 p;
    q4 r;
};

global_var struct Morphable_Terrains
{
    // ---- X and Z values stored as vec2 (binding 0) ----
    GPU_Buffer mesh_xz_values;
    
    GPU_Buffer idx_buffer;
    Model_Handle model_info;


    static constexpr u32 MAX_TERRAINS = 10;
    Morphable_Terrain terrains[MAX_TERRAINS];
    u32 terrain_count {0};

    Pipeline_Handle terrain_ppln;
    Pipeline_Handle terrain_shadow_ppln;

    struct
    {
        Pipeline_Handle ppln;
        // ts_position
        iv2 ts_position{-1};
        // will not be a pointer in the future
        Morphable_Terrain *t;
    } terrain_pointer;
} g_terrains;

internal Morphable_Terrain *
add_terrain(void)
{
    return(&g_terrains.terrains[g_terrains.terrain_count++]);
}    


internal void
clean_up_terrain(GPU *gpu)
{
    for (u32 i = 0; i < g_terrains.terrain_count; ++i)
    {

    }
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

inline m4x4
compute_ws_to_ts_matrix(Morphable_Terrain *t)
{
    m4x4 inverse_translate = glm::translate(-(t->ws_p));
    m4x4 inverse_rotate = glm::transpose(glm::mat4_cast(t->gs_r));
    m4x4 inverse_scale = glm::scale(1.0f / t->size);
    return(inverse_scale * inverse_rotate * inverse_translate);
}

inline m4x4
compute_ts_to_ws_matrix(Morphable_Terrain *t)
{
    m4x4 translate = glm::translate(t->ws_p);
    m4x4 rotate = glm::mat4_cast(t->gs_r);
    m4x4 scale = glm::scale(t->size);
    return(translate * rotate * scale);
}

inline v3
transform_from_ws_to_ts(const v3 &ws_v,
                        Morphable_Terrain *t)
{
    v3 ts_position = t->inverse_transform * v4(ws_v, 1.0f);

    return(ts_position);
}

internal bool
is_on_terrain(const v3 &ws_position,
              Morphable_Terrain *t)
{
    f32 max_x = (f32)(t->xz_dim.x);
    f32 max_z = (f32)(t->xz_dim.y);
    f32 min_x = 0.0f;
    f32 min_z = 0.0f;

    // ---- change ws_position to the space of the terrain space ----
    v3 ts_position = transform_from_ws_to_ts(ws_position, t);
    
    // ---- check if terrain space position is in the xz boundaries ----
    bool is_in_x_boundaries = (ts_position.x > min_x && ts_position.x < max_x);
    bool is_in_z_boundaries = (ts_position.z > min_z && ts_position.z < max_z);
    bool is_on_top          = (ts_position.y > -0.1f);

    return(is_in_x_boundaries && is_in_z_boundaries && is_on_top);
}

template <typename T> internal f32
distance_squared(const T &v)
{
    return(glm::dot(v, v));
}

internal iv2
get_coord_pointing_at(v3 ws_ray_p,
                      const v3 &ws_ray_d,
                      Morphable_Terrain *t,
                      f32 dt,
                      GPU *gpu)
{
    persist constexpr f32 MAX_DISTANCE = 6.0f;
    persist constexpr f32 MAX_DISTANCE_SQUARED = MAX_DISTANCE * MAX_DISTANCE;
    persist constexpr f32 STEP_SIZE = 0.3f;

    m4x4 ws_to_ts = t->inverse_transform;
    v3 ts_ray_p_start = v3(ws_to_ts * v4(ws_ray_p, 1.0f));
    v3 ts_ray_d = glm::normalize(v3(ws_to_ts * v4(ws_ray_d, 0.0f)));
    v3 ts_ray_diff = STEP_SIZE * ts_ray_d;

    iv2 ts_position = iv2(-1);

    for (v3 ts_ray_step = ts_ray_d;
         distance_squared(ts_ray_step) < MAX_DISTANCE_SQUARED;
         ts_ray_step += ts_ray_diff)
    {
	v3 ts_ray_current_p = ts_ray_step + ts_ray_p_start;

	if (ts_ray_current_p.x >= 0.0f && ts_ray_current_p.x < (f32)t->xz_dim.x + 0.000001f
	    && ts_ray_current_p.z >= 0.0f && ts_ray_current_p.z < (f32)t->xz_dim.y + 0.000001f)
	{
	    u32 x = (u32)glm::round(ts_ray_current_p.x / 2.0f) * 2;
	    u32 z = (u32)glm::round(ts_ray_current_p.z / 2.0f) * 2;

	    u32 index = get_terrain_index(x, z, t->xz_dim.y);

	    f32 *heights_ptr = (f32 *)t->heights;
	    if (ts_ray_current_p.y < heights_ptr[index])
	    {
		// ---- hit terrain at this point ----
		ts_position = iv2(x, z);
		break;
	    }
	}
    }

    return(ts_position);
}

struct Detected_Collision_Return
{
    bool detected; v3 ws_at;
};

internal Detected_Collision_Return
detect_terrain_collision(const v3 &ws_p,
                         Morphable_Terrain *t)
{
    m4x4 ws_to_ts = t->inverse_transform;

    v3 ts_p = v3(ws_to_ts * v4(ws_p, 1.0f));

    v2 ts_p_xz = v2(ts_p.x, ts_p.z);

    // is outside the terrain
    if (ts_p_xz.x < 0.0f || ts_p_xz.x > t->xz_dim.x
        ||  ts_p_xz.y < 0.0f || ts_p_xz.y > t->xz_dim.y)
    {
	return {false};
    }

    // position of the player on one tile (square - two triangles)
    v2 ts_position_on_tile = v2(ts_p_xz.x - glm::floor(ts_p_xz.x)
                                , ts_p_xz.y - glm::floor(ts_p_xz.y));

    // starting from (0, 0)
    iv2 ts_tile_corner_position = iv2(glm::floor(ts_p_xz));


    // wrong math probably
    auto get_height_with_offset = [&t, ts_tile_corner_position, ts_position_on_tile](const v2 &offset_a
										     , const v2 &offset_b
										     , const v2 &offset_c) -> f32
	{
	    f32 tl_x = ts_tile_corner_position.x;
	    f32 tl_z = ts_tile_corner_position.y;
	
	    u32 triangle_indices[3] =
	    {
		get_terrain_index(offset_a.x + tl_x, offset_a.y + tl_z, t->xz_dim.y)
		, get_terrain_index(offset_b.x + tl_x, offset_b.y + tl_z, t->xz_dim.y)
		, get_terrain_index(offset_c.x + tl_x, offset_c.y + tl_z, t->xz_dim.y)
	    };

	    f32 *terrain_heights = (f32 *)t->heights;
	    v3 a = v3(offset_a.x, terrain_heights[triangle_indices[0]], offset_a.y);
	    v3 b = v3(offset_b.x, terrain_heights[triangle_indices[1]], offset_b.y);
	    v3 c = v3(offset_c.x, terrain_heights[triangle_indices[2]], offset_c.y);

	    return(barry_centric(a, b, c, ts_position_on_tile));
	};
    
    f32 ts_height;
    
    if (ts_tile_corner_position.x % 2 == 0)
    {
	if (ts_tile_corner_position.y % 2 == 0)
	{
	    if (ts_position_on_tile.y >= ts_position_on_tile.x)
	    {
		ts_height = get_height_with_offset(v2(0.0f, 0.0f)
						   , v2(0.0f, 1.0f)
						   , v2(1.0f, 1.0f));
	    }
	    else
	    {
		ts_height = get_height_with_offset(v2(0.0f, 0.0f)
						   , v2(1.0f, 1.0f)
						   , v2(1.0f, 0.0f));
	    }
	}
	else
	{
	    if (1.0f - ts_position_on_tile.y >= ts_position_on_tile.x)
	    {
		ts_height = get_height_with_offset(v2(0.0f, 1.0f)
						   , v2(1.0f, 1.0f)
						   , v2(1.0f, 0.0f));
	    }
	    else
	    {
		ts_height = get_height_with_offset(v2(0.0f, 1.0f)
						   , v2(1.0f, 0.0f)
						   , v2(0.0f, 0.0f));
	    }
	}
    }
    else
    {
	if (ts_tile_corner_position.y % 2 == 0)
	{
	    if (1.0f - ts_position_on_tile.y >= ts_position_on_tile.x)
	    {
		ts_height = get_height_with_offset(v2(0.0f, 1.0f)
						   , v2(1.0f, 1.0f)
						   , v2(1.0f, 0.0f));
	    }
	    else
	    {
		ts_height = get_height_with_offset(v2(0.0f, 1.0f)
						   , v2(1.0f, 0.0f)
						   , v2(0.0f, 0.0f));
	    }
	}
	else
	{
	    if (ts_position_on_tile.y >= ts_position_on_tile.x)
	    {
		ts_height = get_height_with_offset(v2(0.0f, 0.0f)
						   , v2(0.0f, 1.0f)
						   , v2(1.0f, 1.0f));

		
	    }
	    else
	    {
		ts_height = get_height_with_offset(v2(0.0f, 0.0f)
						   , v2(1.0f, 1.0f)
						   , v2(1.0f, 0.0f));
	    }
	}
    }
    
    // result of the terrain collision in terrain space
    v3 ts_at (ts_p_xz.x, ts_height, ts_p_xz.y);

    v3 ws_at = v3(compute_ts_to_ws_matrix(t) * v4(ts_at, 1.0f));
    
    if (ts_p.y < 0.00000001f + ts_height)
    {
	return {true, ws_at};
    }

    return {false};
}

internal void
morph_terrain_at(const iv2 &ts_position
		 , Morphable_Terrain *t
		 , f32 morph_zone_radius
		 , f32 dt
                 , GPU *gpu)
{
    u32 morph_quotients_outer_count = (morph_zone_radius - 1) * (morph_zone_radius - 1);
    u32 morph_quotients_inner_count = morph_zone_radius * 2 - 1;
    
    struct Morph_Point
    {
	iv2 xz;
	f32 quotient;
    } *morph_quotients_outer_cache = (Morph_Point *)allocate_linear(sizeof(Morph_Point) * morph_quotients_outer_count)
	  , *morph_quotients_inner_cache = (Morph_Point *)allocate_linear(sizeof(Morph_Point) * morph_quotients_inner_count);

    morph_quotients_outer_count = morph_quotients_inner_count = 0;
    
    // ---- one quarter of the mound + prototype the mound modifier quotients for the rest of the 3/4 mounds ----
    for (s32 z = 0; z < morph_zone_radius; ++z)
    {
	for (s32 x = 0; x < morph_zone_radius; ++x)
	{
	    v2 f_coord = v2((f32)x, (f32)z);
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
	    
	    f32 *p = (f32 *)t->heights;
	    f32 a = cos(squared_d / (morph_zone_radius * morph_zone_radius));
	    a = a * a * a;

	    if (index >= 0)
	    {
		p[index] += a * dt;
	    }

	    if (x == 0 || z == 0)
	    {
		morph_quotients_inner_cache[morph_quotients_inner_count++] = Morph_Point{iv2(x, z), a};
	    }
	    else
	    {
		morph_quotients_outer_cache[morph_quotients_outer_count++] = Morph_Point{iv2(x, z), a};
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
	    
	f32 *p = (f32 *)t->heights;
	
	s32 index = get_terrain_index(ts_p_x, ts_p_z, t->xz_dim.x, t->xz_dim.y);
	if (index >= 0)
	{
	    p[index] += morph_quotients_inner_cache[inner].quotient * dt;
	}
    }

    // ---- do other 3/4 of the "outer" of the mound ----
    iv2 mound_quarter_multipliers[] { iv2(+1, -1), iv2(-1, -1), iv2(-1, +1) };
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
	    f32 *p = (f32 *)t->heights;

	    if (index >= 0)
	    {
		p[index] += morph_quotients_outer_cache[outer].quotient * dt;
	    }
	}
    }

    t->is_modified = true;
}

internal Morphable_Terrain *
on_which_terrain(const v3 &ws_position)
{
    for (u32 i = 0; i < g_terrains.terrain_count; ++i)
    {
        if (is_on_terrain(ws_position, &g_terrains.terrains[i]))
        {
            return(&g_terrains.terrains[i]);
        }
    }
    return(nullptr);
}

// ---- this command happens when rendering (terrain is updated on the cpu side at a different time) ----
internal void
update_terrain_on_gpu(GPU_Command_Queue *queue)
{
    for (u32 terrain = 0;
         terrain < g_terrains.terrain_count;
         ++terrain)
    {
        Morphable_Terrain *terrainptr = &g_terrains.terrains[terrain];
        if (terrainptr->is_modified)
        {
            update_gpu_buffer(&terrainptr->heights_gpu_buffer,
                              terrainptr->heights,
                              sizeof(f32) * terrainptr->xz_dim.x * terrainptr->xz_dim.y,
                              0,
                              VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
                              VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT,
                              &queue->q);
            terrainptr->is_modified = false;
        }
    }
}

internal void
make_3D_terrain_base(u32 width_x
		     , u32 depth_z
		     , f32 random_displacement_factor
		     , GPU_Buffer *mesh_xz_values
		     , GPU_Buffer *idx_buffer
		     , Model *model_info
		     , VkCommandPool *cmdpool
		     , GPU *gpu)
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
    invoke_staging_buffer_for_device_local_buffer(Memory_Byte_Buffer{sizeof(f32) * 2 * width_x * depth_z, vtx}
							  , VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT
							  , cmdpool
							  , mesh_xz_values
							  , gpu);

    invoke_staging_buffer_for_device_local_buffer(Memory_Byte_Buffer{sizeof(u32) * 11 * (((width_x - 1) * (depth_z - 1)) / 4), idx} // <--- this is idx, not vtx .... (stupid error)
							  , VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT
							  , cmdpool
							  , idx_buffer
							  , gpu);

    model_info->attribute_count = 2;
    model_info->attributes_buffer = (VkVertexInputAttributeDescription *)allocate_free_list(sizeof(VkVertexInputAttributeDescription) * model_info->attribute_count);
    model_info->binding_count = 2;
    model_info->bindings = (Model_Binding *)allocate_free_list(sizeof(Model_Binding) * model_info->binding_count);
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
			      , GPU_Buffer *gpu_side_heights
			      , VkCommandPool *cmdpool
			      , GPU *gpu)
{
    // Don't need in the future
    cpu_side_heights = (f32 *)allocate_free_list(sizeof(f32) * width_x * depth_z);
    memset(cpu_side_heights, 0, sizeof(f32) * width_x * depth_z);

    init_buffer(adjust_memory_size_for_gpu_alignment(sizeof(f32) * width_x * depth_z, gpu)
			, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT
			, VK_SHARING_MODE_EXCLUSIVE
			, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
			, gpu
			, gpu_side_heights);
}

internal void
make_terrain_mesh_data(u32 w, u32 d, Morphable_Terrain *terrain, VkCommandPool *cmdpool, GPU *gpu)
{
    make_3D_terrain_mesh_instance(w, d, terrain->heights, &terrain->heights_gpu_buffer, cmdpool, gpu);
    terrain->xz_dim = iv2(w, d);
}


// TODO : Make this take roughness and metalness to push to pushconstant or whatever 
internal void
make_terrain_rendering_data(Morphable_Terrain *terrain, GPU_Material_Submission_Queue *queue
                            , const v3 &position, const q4 &rotation, const v3 &size, const v3 &color)
{
    auto *model_info = g_model_manager.get(g_terrains.model_info);
    terrain->vbos[0] = g_terrains.mesh_xz_values.buffer;
    terrain->vbos[1] = terrain->heights_gpu_buffer.buffer;
    g_world_submission_queues[TERRAIN_QUEUE].push_material(&terrain->push_k
                                                           , sizeof(terrain->push_k)
                                                           , {2, terrain->vbos}
                                                           , model_info->index_data
                                                           , init_draw_indexed_data_default(1, model_info->index_data.index_count));

    terrain->ws_p = position;
    terrain->gs_r = rotation;
    terrain->size = size;
    terrain->push_k.color = color;
    terrain->push_k.transform =
        glm::translate(terrain->ws_p)
        * glm::mat4_cast(terrain->gs_r)
        * glm::scale(terrain->size);
    terrain->inverse_transform = compute_ws_to_ts_matrix(terrain);

    terrain->ws_n = v3(glm::mat4_cast(terrain->gs_r) * v4(0.0f, 1.0f, 0.0f, 0.0f));
}



internal void
make_terrain_instances(GPU *gpu, VkCommandPool *cmdpool)
{
    // Make the terrain render command recorder
    g_world_submission_queues[TERRAIN_QUEUE] = make_gpu_material_submission_queue(10
                                                                    , VK_SHADER_STAGE_VERTEX_BIT
                                                                    // Parameter is obsolete, must remove
                                                                    , VK_COMMAND_BUFFER_LEVEL_SECONDARY
                                                                    , cmdpool
                                                                    , gpu);

    auto *red_terrain = add_terrain();
    make_terrain_mesh_data(21, 21, red_terrain, cmdpool, gpu);
    make_terrain_rendering_data(red_terrain, &g_world_submission_queues[TERRAIN_QUEUE]
                                , v3(0.0f, 0.0f, 200.0f)
                                , q4(glm::radians(v3(30.0f, 20.0f, 0.0f)))
                                , v3(15.0f)
                                , v3(255.0f, 69.0f, 0.0f) / 256.0f);

    auto *green_terrain = add_terrain();
    make_terrain_mesh_data(21, 21, green_terrain, cmdpool, gpu);
    make_terrain_rendering_data(green_terrain, &g_world_submission_queues[TERRAIN_QUEUE]
                                , v3(200.0f, 0.0f, 0.0f)
                                , q4(glm::radians(v3(0.0f, 45.0f, 20.0f)))
                                , v3(10.0f)
                                , v3(0.1, 0.6, 0.2) * 0.7f);
}

internal void
make_terrain_pointer(GPU *gpu, Swapchain *swapchain)
{
    //    g_terrains.terrain_pointer.ppln = g_pipeline_manager.get_handle("pipeline.terrain_mesh_pointer_pipeline"_hash);

    g_terrains.terrain_pointer.ppln = g_pipeline_manager.add("pipeline.terrain_mesh_pointer"_hash);
    auto *terrain_pointer_ppln = g_pipeline_manager.get(g_terrains.terrain_pointer.ppln);
    {
        Render_Pass_Handle dfr_render_pass = g_render_pass_manager.get_handle("render_pass.deferred_render_pass"_hash);
        Shader_Modules modules(Shader_Module_Info{"shaders/SPV/terrain_pointer.vert.spv", VK_SHADER_STAGE_VERTEX_BIT},
                               Shader_Module_Info{"shaders/SPV/terrain_pointer.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT});
        Shader_Uniform_Layouts layouts(g_uniform_layout_manager.get_handle("uniform_layout.camera_transforms_ubo"_hash));
        Shader_PK_Data push_k = {160, 0, VK_SHADER_STAGE_VERTEX_BIT};
        Shader_Blend_States blending(false, false, false, false);
        Dynamic_States dynamic(VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_LINE_WIDTH);
        make_graphics_pipeline(terrain_pointer_ppln, modules, VK_FALSE, VK_PRIMITIVE_TOPOLOGY_LINE_LIST, VK_POLYGON_MODE_LINE,
                               VK_CULL_MODE_NONE, layouts, push_k, get_backbuffer_resolution(), blending, nullptr,
                               true, 0.0f, dynamic, g_render_pass_manager.get(dfr_render_pass), 0, gpu);
    }
}

internal void
initialize_terrains(VkCommandPool *cmdpool
                    , Swapchain *swapchain
                    , GPU *gpu)
{
    // ---- register the info of the model for json loader to access ---
    g_terrains.model_info = g_model_manager.add("model.terrain_base_info"_hash);
    auto *model_info = g_model_manager.get(g_terrains.model_info);
    
    make_3D_terrain_base(21, 21
			 , 1.0f
			 , &g_terrains.mesh_xz_values
			 , &g_terrains.idx_buffer
			 , model_info
			 , cmdpool
			 , gpu);

    make_terrain_instances(gpu, cmdpool);

    g_terrains.terrain_ppln = g_pipeline_manager.add("pipeline.terrain_pipeline"_hash);
    auto *terrain_ppln = g_pipeline_manager.get(g_terrains.terrain_ppln);
    {
        Render_Pass_Handle dfr_render_pass = g_render_pass_manager.get_handle("render_pass.deferred_render_pass"_hash);
        Shader_Modules modules(Shader_Module_Info{"shaders/SPV/terrain.vert.spv", VK_SHADER_STAGE_VERTEX_BIT},
                               Shader_Module_Info{"shaders/SPV/terrain.geom.spv", VK_SHADER_STAGE_GEOMETRY_BIT},
                               Shader_Module_Info{"shaders/SPV/terrain.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT});
        Shader_Uniform_Layouts layouts(g_uniform_layout_manager.get_handle("uniform_layout.camera_transforms_ubo"_hash),
                                       g_uniform_layout_manager.get_handle("descriptor_set_layout.2D_sampler_layout"_hash));
        Shader_PK_Data push_k = {160, 0, VK_SHADER_STAGE_VERTEX_BIT};
        Shader_Blend_States blending(false, false, false, false);
        Dynamic_States dynamic(VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_LINE_WIDTH);
        make_graphics_pipeline(terrain_ppln, modules, VK_TRUE, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN, VK_POLYGON_MODE_FILL,
                               VK_CULL_MODE_NONE, layouts, push_k, get_backbuffer_resolution(), blending, model_info,
                               true, 0.0f, dynamic, g_render_pass_manager.get(dfr_render_pass), 0, gpu);
    }

    g_terrains.terrain_shadow_ppln = g_pipeline_manager.add("pipeline.terrain_shadow"_hash);
    auto *terrain_shadow_ppln = g_pipeline_manager.get(g_terrains.terrain_shadow_ppln);
    {
        auto shadow_display = get_shadow_display();
        VkExtent2D shadow_extent = VkExtent2D{shadow_display.shadowmap_w, shadow_display.shadowmap_h};
        Render_Pass_Handle shadow_render_pass = g_render_pass_manager.get_handle("render_pass.shadow_render_pass"_hash);
        Shader_Modules modules(Shader_Module_Info{"shaders/SPV/terrain_shadow.vert.spv", VK_SHADER_STAGE_VERTEX_BIT},
                               Shader_Module_Info{"shaders/SPV/terrain_shadow.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT});
        Shader_Uniform_Layouts layouts(g_uniform_layout_manager.get_handle("uniform_layout.camera_transforms_ubo"_hash));
        Shader_PK_Data push_k = {160, 0, VK_SHADER_STAGE_VERTEX_BIT};
        Shader_Blend_States blending = {};
        Dynamic_States dynamic(VK_DYNAMIC_STATE_DEPTH_BIAS, VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_LINE_WIDTH);
        make_graphics_pipeline(terrain_shadow_ppln, modules, VK_TRUE, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN, VK_POLYGON_MODE_FILL,
                               VK_CULL_MODE_NONE, layouts, push_k, shadow_extent, blending, model_info,
                               true, 0.0f, dynamic, g_render_pass_manager.get(shadow_render_pass), 0, gpu);
    }
    
    make_terrain_pointer(gpu, swapchain);
}

internal void
prepare_terrain_pointer_for_render(GPU_Command_Queue *queue
				   , VkDescriptorSet *ubo_set)
{
    // if the get_coord_pointing_at returns a coord with a negative - player is not pointing at the terrain
    if (g_terrains.terrain_pointer.ts_position.x >= 0)
    {
	auto *ppln = g_pipeline_manager.get(g_terrains.terrain_pointer.ppln);
	command_buffer_bind_pipeline(ppln
					     , &queue->q);

	command_buffer_bind_descriptor_sets(ppln
						    , {1, ubo_set}
						    , &queue->q);

	struct
	{
	    m4x4 ts_to_ws_terrain_model;
	    v4 color;
	    v4 ts_center_position;
	    // center first
	    f32 ts_heights[8];
	} push_k;

	push_k.ts_to_ws_terrain_model = g_terrains.terrain_pointer.t->push_k.transform;
	push_k.color = v4(1.0f);
	push_k.ts_center_position = v4((f32)g_terrains.terrain_pointer.ts_position.x
					      , 0.0f
					      , (f32)g_terrains.terrain_pointer.ts_position.y
					      , 1.0f);

	u32 x = g_terrains.terrain_pointer.ts_position.x;
	u32 z = g_terrains.terrain_pointer.ts_position.y;
	u32 width = g_terrains.terrain_pointer.t->xz_dim.x;
	u32 depth = g_terrains.terrain_pointer.t->xz_dim.y;
	f32 *heights = (f32 *)(g_terrains.terrain_pointer.t->heights);

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
    
	command_buffer_push_constant(&push_k
                                     , sizeof(push_k)
                                     , 0
                                     , VK_SHADER_STAGE_VERTEX_BIT
                                     , ppln
                                     , &queue->q);

	command_buffer_draw(&queue->q
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
    
    v3 gravity_force_accumulation = {};

    bool enabled;
    
    // other forces (friction...)
};

struct Camera_Component
{
    u32 entity_index;
    
    // Can be set to -1, in that case, there is no camera bound
    Camera_Handle camera{-1};

    // Maybe some other variables to do with 3rd person / first person configs ...

    // Variable allows for smooth animation between up vectors when switching terrains
    bool in_animation = false;
    q4 current_rotation;
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
	m4x4 ws_t{1.0f};
	v4 color;
    } push_k;
};

struct Entity
{
    Entity(void) = default;
    
    Constant_String id {""_hash};
    // position, direction, velocity
    // in above entity group space
    v3 ws_p{0.0f}, ws_d{0.0f}, ws_v{0.0f}, ws_input_v{0.0f};
    q4 ws_r{0.0f, 0.0f, 0.0f, 0.0f};
    v3 size{1.0f};

    // For now is a pointer - is not a component because all entities will have one
    // This is the last terrain that the player was on / is still on
    // Is used for collision detection and also the camera view matrix (up vector...)
    Morphable_Terrain *on_t = nullptr;
    bool is_on_terrain = false;

    static constexpr f32 SWITCH_TERRAIN_ANIMATION_TIME = 0.6f;
    
    bool switch_terrain_animation_mode = false;
    q4 previous_terrain_rot;
    q4 current_rot;
    f32 animation_time = 0.0f;

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

    Pipeline_Handle entity_ppln;
    Pipeline_Handle entity_shadow_ppln;

    Model_Handle entity_model;

    // For now:
    u32 main_entity;
    
    // have some sort of stack of REMOVED entities
} g_entities;

Entity
construct_entity(const Constant_String &name
		 //		 , Entity::Is_Group is_group
		 , v3 gs_p
		 , v3 ws_d
		 , q4 gs_r)
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
    Entity_Handle v = *g_entities.name_map.get(name.hash);
    return(&g_entities.entity_list[v]);
}

internal Entity *
get_entity(Entity_Handle v)
{
    return(&g_entities.entity_list[v]);
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
    e->components.camera_component = g_entities.camera_component_count++;
    Camera_Component *component = &g_entities.camera_components[ e->components.camera_component ];
    component->entity_index = e->index;
    component->camera = camera_index;

    return(component);
}

internal void
update_camera_components(f32 dt)
{
    for (u32 i = 0; i < g_entities.camera_component_count; ++i)
    {
        Camera_Component *component = &g_entities.camera_components[ i ];
        Camera *camera = get_camera(component->camera);
        Entity *e = &g_entities.entity_list[ component->entity_index ];

        v3 up = v3(0.0f, 1.0f, 0.0f);
        if (e->on_t)
        {
            up = e->on_t->ws_n;
            if (e->switch_terrain_animation_mode)
            {
                up = v3(glm::mat4_cast(e->current_rot) * v4(0.0f, 1.0f, 0.0f, 1.0f));
            }
        }
        
        camera->v_m = glm::lookAt(e->ws_p + e->on_t->ws_n
                                  , e->ws_p + e->on_t->ws_n + e->ws_d
                                  , up);

        // TODO: Don't need to calculate this every frame, just when parameters change
        camera->compute_projection();

        camera->p = e->ws_p;
        camera->d = e->ws_d;
        camera->u = up;
    }
}

internal Rendering_Component *
add_rendering_component(Entity *e)
{
    e->components.rendering_component = g_entities.rendering_component_count++;
    Rendering_Component *component = &g_entities.rendering_components[ e->components.rendering_component ];
    component->entity_index = e->index;
    component->push_k = {};

    return(component);
}

internal void
update_rendering_component(f32 dt)
{
    for (u32 i = 0; i < g_entities.rendering_component_count; ++i)
    {
        Rendering_Component *component = &g_entities.rendering_components[ i ];
        Entity *e = &g_entities.entity_list[ component->entity_index ];
        
        if (e->on_t)
        {
            component->push_k.ws_t = glm::translate(e->ws_p) * glm::mat4_cast(e->current_rot) * glm::scale(e->size);
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
    e->components.physics_component = g_entities.physics_component_count++;
    Physics_Component *component = &g_entities.physics_components[ e->components.physics_component ];
    component->entity_index = e->index;
    component->enabled = enabled;

    return(component);
}

internal void
update_physics_components(f32 dt)
{
    for (u32 i = 0; i < g_entities.physics_component_count; ++i)
    {
        Physics_Component *component = &g_entities.physics_components[ i ];
        Entity *e = &g_entities.entity_list[ component->entity_index ];

        auto *which_terrain = on_which_terrain(e->ws_p);
        if (which_terrain)
        {
            if (which_terrain == e->on_t)
            {
                e->is_on_terrain = true;
            }
            else
            {
                // Switch terrains!
                e->is_on_terrain = true;
                q4 previous = q4(glm::radians(0.0f), v3(0.0f, 0.0f, 0.0f));
                e->previous_terrain_rot = previous;
                if (e->on_t)
                {
                    e->previous_terrain_rot = e->on_t->gs_r;
                }
                e->switch_terrain_animation_mode = true;
                e->animation_time = 0.0f;
                e->on_t = which_terrain;
            }
        }
        else
        {
            e->is_on_terrain = false;
        }
        
        if (component->enabled)
        {
            Morphable_Terrain *t = e->on_t;

            v3 gravity_d = -9.5f * t->ws_n;

            Detected_Collision_Return ret = detect_terrain_collision(e->ws_p, e->on_t);
    
            if (ret.detected)
            {
                // implement coefficient of restitution
                e->ws_v = v3(0.0f);
                gravity_d = v3(0.0f);
                e->ws_p = ret.ws_at;
            }
    
            e->ws_v += gravity_d * dt;

            e->ws_p += (e->ws_v + e->ws_input_v) * dt;
        }
        else
        {
            e->ws_p += e->ws_input_v * dt;
        }

        if (e->animation_time > Entity::SWITCH_TERRAIN_ANIMATION_TIME)
        {
            e->switch_terrain_animation_mode = false;
        }
        
        if (e->switch_terrain_animation_mode && e->on_t)
        {
            e->animation_time += dt;
            e->current_rot = glm::mix(e->previous_terrain_rot, e->on_t->gs_r, e->animation_time / Entity::SWITCH_TERRAIN_ANIMATION_TIME);
        }
        else
        {
            e->current_rot = e->on_t->gs_r;
        }
    }
}

internal Input_Component *
add_input_component(Entity *e)
{
    e->components.input_component = g_entities.input_component_count++;
    Input_Component *component = &g_entities.input_components[ e->components.input_component ];
    component->entity_index = e->index;

    return(component);
}

// Don't even know yet if this is needed ? Only one entity will have this component - maybe just keep for consistency with the system
internal void
update_input_components(Window_Data *window
                        , f32 dt
                        , GPU *gpu)
{
    for (u32 i = 0; i < g_entities.input_component_count; ++i)
    {
        Input_Component *component = &g_entities.input_components[i];
        Entity *e = &g_entities.entity_list[component->entity_index];
        Physics_Component *e_physics = &g_entities.physics_components[e->components.physics_component];

        v3 up = e->on_t->ws_n;
        
        // Mouse movement
        if (window->m_moved)
        {
            // TODO: Make sensitivity configurable with a file or something, and later menu
            persist constexpr u32 SENSITIVITY = 15.0f;
    
            v2 prev_mp = v2(window->prev_m_x, window->prev_m_y);
            v2 curr_mp = v2(window->m_x, window->m_y);

            v3 res = e->ws_d;
	    
            v2 d = (curr_mp - prev_mp);

            f32 x_angle = glm::radians(-d.x) * SENSITIVITY * dt;// *elapsed;
            f32 y_angle = glm::radians(-d.y) * SENSITIVITY * dt;// *elapsed;
            res = m3x3(glm::rotate(x_angle, up)) * res;
            v3 rotate_y = glm::cross(res, up);
            res = m3x3(glm::rotate(y_angle, rotate_y)) * res;

            e->ws_d = res;
        }

        // Mouse input
        iv2 ts_coord = get_coord_pointing_at(e->ws_p
                                             , e->ws_d
                                             , e->on_t
                                             , dt
                                             , gpu);
        g_terrains.terrain_pointer.ts_position = ts_coord;
        g_terrains.terrain_pointer.t = e->on_t;
    
        // ---- modify the terrain ----
        if (window->mb_map[GLFW_MOUSE_BUTTON_RIGHT])
        {
            if (ts_coord.x >= 0)
            {
                morph_terrain_at(ts_coord, e->on_t, 3, dt, gpu);
            }
        }

        // Keyboard input for entity
        u32 movements = 0;
        f32 accelerate = 1.0f;
    
        auto acc_v = [&movements, &accelerate](const v3 &d, v3 &dst){ ++movements; dst += d * accelerate; };

        v3 d = glm::normalize(v3(e->ws_d.x
                                               , e->ws_d.y
                                               , e->ws_d.z));

        Morphable_Terrain *t = e->on_t;
        m4x4 inverse = t->inverse_transform;
    
        v3 ts_d = inverse * v4(d, 0.0f);
    
        ts_d.y = 0.0f;

        d = v3(t->push_k.transform * v4(ts_d, 0.0f));
        d = glm::normalize(d);
    
        v3 res = {};

        bool detected_collision = detect_terrain_collision(e->ws_p, e->on_t).detected;
    
        if (detected_collision) e->ws_v = v3(0.0f);
    
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
            e->ws_input_v = v3(0.0f);
        }
    }
}

internal Entity_Handle
add_entity(const Entity &e)

{
    Entity_Handle view;
    view = g_entities.entity_count;

    g_entities.name_map.insert(e.id.hash, view);
    
    g_entities.entity_list[g_entities.entity_count++] = e;

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

    Rendering_Component *component = &g_entities.rendering_components[ e_ptr->components.rendering_component ];
    
    queue->push_material(&component->push_k
			 , sizeof(component->push_k)
			 , model->raw_cache_for_rendering
			 , model->index_data
			 , init_draw_indexed_data_default(1, model->index_data.index_count));
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
                , GPU *gpu)
{
    update_input_components(window, dt, gpu);
    update_physics_components(dt);
    update_camera_components(dt);
    update_rendering_component(dt);
}

internal void
initialize_entities(GPU *gpu, Swapchain *swapchain, VkCommandPool *cmdpool, Window_Data *window)
{
    g_entities.entity_model = g_model_manager.get_handle("model.cube_model"_hash);
    
    g_entities.entity_ppln = g_pipeline_manager.add("pipeline.model"_hash);
    auto *entity_ppln = g_pipeline_manager.get(g_entities.entity_ppln);
    {
        Model_Handle cube_hdl = g_model_manager.get_handle("model.cube_model"_hash);
        Render_Pass_Handle dfr_render_pass = g_render_pass_manager.get_handle("render_pass.deferred_render_pass"_hash);
        Shader_Modules modules(Shader_Module_Info{"shaders/SPV/model.vert.spv", VK_SHADER_STAGE_VERTEX_BIT},
                               Shader_Module_Info{"shaders/SPV/model.geom.spv", VK_SHADER_STAGE_GEOMETRY_BIT},
                               Shader_Module_Info{"shaders/SPV/model.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT});
        Shader_Uniform_Layouts layouts(g_uniform_layout_manager.get_handle("uniform_layout.camera_transforms_ubo"_hash),
                                       g_uniform_layout_manager.get_handle("descriptor_set_layout.2D_sampler_layout"_hash));
        Shader_PK_Data push_k = {160, 0, VK_SHADER_STAGE_VERTEX_BIT};
        Shader_Blend_States blending(false, false, false, false);
        Dynamic_States dynamic(VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_LINE_WIDTH);
        make_graphics_pipeline(entity_ppln, modules, VK_FALSE, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_POLYGON_MODE_FILL,
                               VK_CULL_MODE_NONE, layouts, push_k, get_backbuffer_resolution(), blending, g_model_manager.get(cube_hdl),
                               true, 0.0f, dynamic, g_render_pass_manager.get(dfr_render_pass), 0, gpu);
    }

    g_entities.entity_shadow_ppln = g_pipeline_manager.add("pipeline.model_shadow"_hash);
    auto *entity_shadow_ppln = g_pipeline_manager.get(g_entities.entity_shadow_ppln);
    {
        auto shadow_display = get_shadow_display();
        VkExtent2D shadow_extent {shadow_display.shadowmap_w, shadow_display.shadowmap_h};
        Model_Handle cube_hdl = g_model_manager.get_handle("model.cube_model"_hash);
        Render_Pass_Handle shadow_render_pass = g_render_pass_manager.get_handle("render_pass.shadow_render_pass"_hash);
        Shader_Modules modules(Shader_Module_Info{"shaders/SPV/model_shadow.vert.spv", VK_SHADER_STAGE_VERTEX_BIT},
                               Shader_Module_Info{"shaders/SPV/model_shadow.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT});
        Shader_Uniform_Layouts layouts(g_uniform_layout_manager.get_handle("uniform_layout.camera_transforms_ubo"_hash));
        Shader_PK_Data push_k = {160, 0, VK_SHADER_STAGE_VERTEX_BIT};
        Shader_Blend_States blending(false);
        Dynamic_States dynamic(VK_DYNAMIC_STATE_DEPTH_BIAS, VK_DYNAMIC_STATE_VIEWPORT);
        make_graphics_pipeline(entity_shadow_ppln, modules, VK_FALSE, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_POLYGON_MODE_FILL,
                               VK_CULL_MODE_NONE, layouts, push_k, shadow_extent, blending, g_model_manager.get(cube_hdl),
                               true, 0.0f, dynamic, g_render_pass_manager.get(shadow_render_pass), 0, gpu);
    }

    g_world_submission_queues[ENTITY_QUEUE] = make_gpu_material_submission_queue(20
                                                                                 , VK_SHADER_STAGE_VERTEX_BIT
                                                                                 , VK_COMMAND_BUFFER_LEVEL_SECONDARY
                                                                                 , cmdpool
                                                                                 , gpu);

    Entity e = construct_entity("entity.bound_group_test0"_hash
				, v3(50.0f, 10.0f, 280.0f)
				, glm::normalize(v3(1.0f, 0.0f, 1.0f))
				, q4(0, 0, 0, 0));

    Entity_Handle ev = add_entity(e);
    auto *e_ptr = get_entity(ev);
    g_entities.main_entity = ev;

    e_ptr->on_t = on_which_terrain(e.ws_p);

    add_physics_component(e_ptr, false);    
    auto *camera_component_ptr = add_camera_component(e_ptr, add_camera(window, get_backbuffer_resolution()));
    add_input_component(e_ptr);

    bind_camera_to_3D_scene_output(camera_component_ptr->camera);
        
    // add rotating entity
    Entity r = construct_entity("entity.rotating"_hash
				, v3(200.0f, -40.0f, 300.0f)
				, v3(0.0f)
				, q4(glm::radians(45.0f), v3(0.0f, 1.0f, 0.0f)));

    r.size = v3(10.0f);

    Entity_Handle rv = add_entity(r);
    auto *r_ptr = get_entity(rv);
    
    r_ptr->on_t = &g_terrains.terrains[0];

    Rendering_Component *r_ptr_rendering = add_rendering_component(r_ptr);
    add_physics_component(r_ptr, true);
    
    r_ptr_rendering->push_k.color = v4(0.2f, 0.2f, 0.8f, 1.0f);
    
    push_entity_to_queue(r_ptr
                         , g_model_manager.get_handle("model.cube_model"_hash)
                         , &g_world_submission_queues[ENTITY_QUEUE]);

    Entity r2 = construct_entity("entity.rotating2"_hash
				 , v3(250.0f, -40.0f, 350.0f)
				 , v3(0.0f)
				 , q4(glm::radians(45.0f), v3(0.0f, 1.0f, 0.0f)));

    r2.size = v3(10.0f);
    Entity_Handle rv2 = add_entity(r2);
    auto *r2_ptr = get_entity(rv2);

    Rendering_Component *r2_ptr_rendering = add_rendering_component(r2_ptr);
    add_physics_component(r2_ptr, false);
    
    r2_ptr_rendering->push_k.color = v4(0.6f, 0.0f, 0.6f, 1.0f);
    r2_ptr->on_t = &g_terrains.terrains[0];

    push_entity_to_queue(r2_ptr
                         , g_model_manager.get_handle("model.cube_model"_hash)
                         , &g_world_submission_queues[ENTITY_QUEUE]);
}

// ---- rendering of the entire world happens here ----
internal void
prepare_terrain_pointer_for_render(VkCommandBuffer *cmdbuf, VkDescriptorSet *set, Framebuffer *fbo);

internal void
render_world(Vulkan_State *vk
	     , u32 image_index
	     , u32 current_frame
	     , GPU_Command_Queue *queue)
{
    // Fetch some data needed to render
    auto transforms_ubo_uniform_groups = get_camera_transform_uniform_groups();
    Shadow_Display shadow_display_data = get_shadow_display();
    
    Uniform_Group uniform_groups[2] = {transforms_ubo_uniform_groups[image_index], shadow_display_data.texture};

    Camera *camera = get_camera_bound_to_3D_output();

    // Update terrain gpu buffers
    update_terrain_on_gpu(queue);
    
    // Rendering to the shadow map
    begin_shadow_offscreen(4000, 4000, queue);
    {
        auto *model_ppln = g_pipeline_manager.get(g_entities.entity_shadow_ppln);

        g_world_submission_queues[ENTITY_QUEUE].submit_queued_materials({1, &transforms_ubo_uniform_groups[image_index]}, model_ppln
                                                          , queue
                                                          , VK_COMMAND_BUFFER_LEVEL_PRIMARY);

        auto *terrain_ppln = g_pipeline_manager.get(g_terrains.terrain_shadow_ppln);    
    
        g_world_submission_queues[TERRAIN_QUEUE].submit_queued_materials({1, &transforms_ubo_uniform_groups[image_index]}, terrain_ppln
                                                           , queue
                                                           , VK_COMMAND_BUFFER_LEVEL_PRIMARY);
    }
    end_shadow_offscreen(queue);

    // Rendering the scene with lighting and everything
    begin_deferred_rendering(image_index, queue);
    {
        auto *terrain_ppln = g_pipeline_manager.get(g_terrains.terrain_ppln);    
        auto *entity_ppln = g_pipeline_manager.get(g_entities.entity_ppln);
    
        g_world_submission_queues[TERRAIN_QUEUE].submit_queued_materials({2, uniform_groups}, terrain_ppln, queue, VK_COMMAND_BUFFER_LEVEL_PRIMARY);
        g_world_submission_queues[ENTITY_QUEUE].submit_queued_materials({2, uniform_groups}, entity_ppln, queue, VK_COMMAND_BUFFER_LEVEL_PRIMARY);

        prepare_terrain_pointer_for_render(queue, &transforms_ubo_uniform_groups[image_index]);

        render_3D_frustum_debug_information(queue, image_index);
        
        // ---- render skybox ----
        auto *cube_model = g_model_manager.get(g_entities.entity_model);
        render_atmosphere({1, uniform_groups}, camera->p, cube_model, queue);
    }
    end_deferred_rendering(camera->v_m, queue);

    apply_pfx_on_scene(queue, &transforms_ubo_uniform_groups[image_index], camera->v_m, camera->p_m, &vk->gpu);
}

void
initialize_world(Window_Data *window
                 , Vulkan_State *vk
                 , VkCommandPool *cmdpool)
{
    initialize_terrains(cmdpool, &vk->swapchain, &vk->gpu);
    initialize_entities(&vk->gpu, &vk->swapchain, cmdpool, window);
}

void
update_world(Window_Data *window
	     , Vulkan_State *vk
	     , f32 dt
	     , u32 image_index
	     , u32 current_frame
	     , GPU_Command_Queue *cmdbuf)
{
    handle_input_debug(window, dt, &vk->gpu);
    
    update_entities(window, dt, &vk->gpu);
    

    // ---- Actually rendering the frame ----
    update_3D_output_camera_transforms(image_index, &vk->gpu);
    
    render_world(vk, image_index, current_frame, cmdbuf);    
}


#include <glm/gtx/string_cast.hpp>


// Not to do with moving the entity, just debug stuff : will be used later for stuff like opening menus
void
handle_input_debug(Window_Data *window
                   , f32 dt
                   , GPU *gpu)
{
    // ---- get bound entity ----
    // TODO make sure to check if main_entity < 0
    Entity *e_ptr = &g_entities.entity_list[g_entities.main_entity];
    Camera_Component *e_camera_component = &g_entities.camera_components[e_ptr->components.camera_component];
    Physics_Component *e_physics = &g_entities.physics_components[e_ptr->components.physics_component];
    Camera *e_camera = get_camera(e_camera_component->camera);
    v3 up = e_ptr->on_t->ws_n;
    
    Shadow_Matrices shadow_data = get_shadow_matrices();
    Shadow_Debug    shadow_debug = get_shadow_debug();
    
    //    shadow_data.light_view_matrix = glm::lookAt(v3(0.0f), -glm::normalize(light_pos), v3(0.0f, 1.0f, 0.0f));

    if (window->key_map[GLFW_KEY_C])
    {
	for (u32 i = 0; i < 8; ++i)
	{
	    e_camera->captured_frustum_corners[i] = shadow_debug.frustum_corners[i];
	}

	e_camera->captured_shadow_corners[0] = v4(shadow_debug.x_min, shadow_debug.y_max, shadow_debug.z_min, 1.0f);
	e_camera->captured_shadow_corners[1] = v4(shadow_debug.x_max, shadow_debug.y_max, shadow_debug.z_min, 1.0f);
	e_camera->captured_shadow_corners[2] = v4(shadow_debug.x_max, shadow_debug.y_min, shadow_debug.z_min, 1.0f);
	e_camera->captured_shadow_corners[3] = v4(shadow_debug.x_min, shadow_debug.y_min, shadow_debug.z_min, 1.0f);

	e_camera->captured_shadow_corners[4] = v4(shadow_debug.x_min, shadow_debug.y_max, shadow_debug.z_max, 1.0f);
	e_camera->captured_shadow_corners[5] = v4(shadow_debug.x_max, shadow_debug.y_max, shadow_debug.z_max, 1.0f);
	e_camera->captured_shadow_corners[6] = v4(shadow_debug.x_max, shadow_debug.y_min, shadow_debug.z_max, 1.0f);
	e_camera->captured_shadow_corners[7] = v4(shadow_debug.x_min, shadow_debug.y_min, shadow_debug.z_max, 1.0f);
    }
}



void
destroy_world(GPU *gpu)
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

    destroy_graphics(gpu);
}
