#include "ui.hpp"
#include <chrono>
#include "script.hpp"
#include <iostream>
#include "world.hpp"
#include <glm/glm.hpp>
#include <GLFW/glfw3.h>
#include <glm/gtx/transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include "graphics.hpp"

#define MAX_ENTITIES_UNDER_TOP 10
#define MAX_ENTITIES_UNDER_PLANET 150

constexpr float32_t PI = 3.14159265359f;

global_var constexpr uint32_t MAX_MTRLS = 10;
global_var gpu_material_submission_queue_t g_world_submission_queues[MAX_MTRLS];

enum { ENTITY_QUEUE, TERRAIN_QUEUE };

// ---- terrain code ----
struct morphable_terrain_t
{
    // Gravity constant
    float32_t k_g;
    
    bool is_modified = false;
    
    ivector2_t xz_dim;
    // ---- up vector of the terrain ----
    vector3_t ws_n;
    float32_t *heights;

    vector3_t size;
    vector3_t ws_p;
    quaternion_t gs_r;

    uint32_t offset_into_heights_gpu_buffer;
    // ---- later on this will be a pointer (index into g_gpu_buffer_manager)
    gpu_buffer_t heights_gpu_buffer;
    
    VkBuffer vbos[2];

    matrix4_t inverse_transform;
    struct push_k_t
    {
        matrix4_t transform;
        vector3_t color;
    } push_k;
};

struct planet_t
{
    // planet has 6 faces
    morphable_terrain_t meshes[6];

    vector3_t p;
    quaternion_t r;
};

struct terrain_create_staging
{
    uint32_t dimensions;
    float32_t size;
    vector3_t ws_p;
    vector3_t rotation;
    vector3_t color;
};

struct terrain_triangle_t
{
    bool triangle_exists;
    float32_t ts_height;
    vector3_t ws_exact_pointed_at;
    vector3_t ws_triangle_position[3];
    // Used for morphing function
    ivector2_t offsets[4];
    // Indices
    uint32_t idx[4];
};

global_var struct morphable_terrains_t
{
    // ---- X and Z values stored as vec2 (binding 0) ----
    gpu_buffer_t mesh_xz_values;
    
    gpu_buffer_t idx_buffer;
    model_handle_t model_info;


    static constexpr uint32_t MAX_TERRAINS = 10;
    morphable_terrain_t terrains[MAX_TERRAINS];
    uint32_t terrain_count {0};

    terrain_create_staging create_stagings[10];
    uint32_t create_count = 0;
    
    pipeline_handle_t terrain_ppln;
    pipeline_handle_t terrain_shadow_ppln;

    struct
    {
        pipeline_handle_t ppln;
        terrain_triangle_t triangle;
        // will not be a pointer in the future
        morphable_terrain_t *t;
    } terrain_pointer;
} g_terrains;

internal vector3_t
get_ws_terrain_vertex_position(uint32_t idx,
                               morphable_terrain_t *terrain)
{
    return(vector3_t());
}

internal morphable_terrain_t *
add_terrain(void)
{
    return(&g_terrains.terrains[g_terrains.terrain_count++]);
}    

internal void
clean_up_terrain(gpu_t *gpu)
{
    for (uint32_t i = 0; i < g_terrains.terrain_count; ++i)
    {

    }
}

inline uint32_t
get_terrain_index(uint32_t x, uint32_t z, uint32_t width_x)
{
    return(x + z * width_x);
}

inline int32_t
get_terrain_index(int32_t x, int32_t z, int32_t width_x, int32_t depth_z)
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

inline ivector2_t
get_ts_xz_coord_from_idx(uint32_t idx, morphable_terrain_t *t)
{
    ivector2_t ret = {};
    ret.x = idx % t->xz_dim.x;
    ret.y = (idx - ret.x) / t->xz_dim.x;
    return(ret);
}

inline matrix4_t
compute_ws_to_ts_matrix(morphable_terrain_t *t)
{
    matrix4_t inverse_translate = glm::translate(-(t->ws_p));
    matrix4_t inverse_rotate = glm::transpose(glm::mat4_cast(t->gs_r));
    matrix4_t inverse_scale = glm::scale(1.0f / t->size);
    return(inverse_scale * inverse_rotate * inverse_translate);
}

inline matrix4_t
compute_ts_to_ws_matrix(morphable_terrain_t *t)
{
    matrix4_t translate = glm::translate(t->ws_p);
    matrix4_t rotate = glm::mat4_cast(t->gs_r);
    matrix4_t scale = glm::scale(t->size);
    return(translate * rotate * scale);
}

inline vector3_t
transform_from_ws_to_ts(const vector3_t &ws_v,
                        morphable_terrain_t *t)
{
    vector3_t ts_position = t->inverse_transform * vector4_t(ws_v, 1.0f);

    return(ts_position);
}

internal bool
is_on_terrain(const vector3_t &ws_position,
              morphable_terrain_t *t,
              float32_t *distance)
{
    float32_t max_x = (float32_t)(t->xz_dim.x);
    float32_t max_z = (float32_t)(t->xz_dim.y);
    float32_t min_x = 0.0f;
    float32_t min_z = 0.0f;

    // ---- change ws_position to the space of the terrain space ----
    vector3_t ts_position = transform_from_ws_to_ts(ws_position, t);
    
    // ---- check if terrain space position is in the xz boundaries ----
    bool is_in_x_boundaries = (ts_position.x > min_x && ts_position.x < max_x);
    bool is_in_z_boundaries = (ts_position.z > min_z && ts_position.z < max_z);
    bool is_on_top          = (ts_position.y > -0.1f);

    *distance = ts_position.y;
    
    return(is_in_x_boundaries && is_in_z_boundaries && is_on_top);
}

template <typename T> internal float32_t
distance_squared(const T &v)
{
    return(glm::dot(v, v));
}

internal terrain_triangle_t
get_triangle_from_pos(const vector3_t ts_p,
                      morphable_terrain_t *t)
{
    vector2_t ts_p_xz = vector2_t(ts_p.x, ts_p.z);

    // is outside the terrain
    if (ts_p_xz.x < 0.0f || ts_p_xz.x > t->xz_dim.x
        ||  ts_p_xz.y < 0.0f || ts_p_xz.y > t->xz_dim.y)
    {
	return {false};
    }

    // position of the player on one tile (square - two triangles)
    vector2_t ts_position_on_tile = vector2_t(ts_p_xz.x - glm::floor(ts_p_xz.x)
                                              , ts_p_xz.y - glm::floor(ts_p_xz.y));

    // starting from (0, 0)
    ivector2_t ts_tile_corner_position = ivector2_t(glm::floor(ts_p_xz));


    // wrong math probably
    auto get_height_with_offset = [&t, ts_tile_corner_position, ts_position_on_tile](const vector2_t &offset_a,
										     const vector2_t &offset_b,
										     const vector2_t &offset_c,
                                                                                     // For morphing function
                                                                                     const vector2_t &offset_d) -> terrain_triangle_t
	{
	    float32_t tl_x = ts_tile_corner_position.x;
	    float32_t tl_z = ts_tile_corner_position.y;
	
	    uint32_t triangle_indices[3] =
	    {
		get_terrain_index(offset_a.x + tl_x, offset_a.y + tl_z, t->xz_dim.x)
		, get_terrain_index(offset_b.x + tl_x, offset_b.y + tl_z, t->xz_dim.x)
		, get_terrain_index(offset_c.x + tl_x, offset_c.y + tl_z, t->xz_dim.x)
	    };

	    float32_t *terrain_heights = (float32_t *)t->heights;
	    vector3_t a = vector3_t(offset_a.x, terrain_heights[triangle_indices[0]], offset_a.y);
	    vector3_t b = vector3_t(offset_b.x, terrain_heights[triangle_indices[1]], offset_b.y);
	    vector3_t c = vector3_t(offset_c.x, terrain_heights[triangle_indices[2]], offset_c.y);

            terrain_triangle_t triangle = {};
            triangle.ts_height = barry_centric(a, b, c, ts_position_on_tile);
            triangle.idx[0] = triangle_indices[0];
            triangle.idx[1] = triangle_indices[1];
            triangle.idx[2] = triangle_indices[2];
            // For morphing function
            triangle.idx[3] = get_terrain_index(offset_d.x + tl_x, offset_d.y + tl_z, t->xz_dim.x);
            
            // For now still in terrain space, get converted later
            triangle.ws_triangle_position[0] = vector3_t(offset_a.x + tl_x, a.y, offset_a.y + tl_z);
            triangle.ws_triangle_position[1] = vector3_t(offset_b.x + tl_x, b.y, offset_b.y + tl_z);
            triangle.ws_triangle_position[2] = vector3_t(offset_c.x + tl_x, c.y, offset_c.y + tl_z);
            // For morphing function
            triangle.offsets[0] = ivector2_t(offset_a.x, offset_a.y);
            triangle.offsets[1] = ivector2_t(offset_b.x, offset_b.y);
            triangle.offsets[2] = ivector2_t(offset_c.x, offset_c.y);
            triangle.offsets[3] = ivector2_t(offset_d.x, offset_d.y);
	    return(triangle);
	};
    
    terrain_triangle_t ret = {};

    vector3_t normal;
    
    if (ts_tile_corner_position.x % 2 == 0)
    {
	if (ts_tile_corner_position.y % 2 == 0)
	{
	    if (ts_position_on_tile.y >= ts_position_on_tile.x)
	    {
		ret = get_height_with_offset(vector2_t(0.0f, 0.0f),
                                             vector2_t(0.0f, 1.0f),
                                             vector2_t(1.0f, 1.0f),
                                             vector2_t(1.0f, 0.0f));
	    }
	    else
	    {
		ret = get_height_with_offset(vector2_t(0.0f, 0.0f),
                                             vector2_t(1.0f, 1.0f),
                                             vector2_t(1.0f, 0.0f),
                                             vector2_t(0.0f, 1.0f));
	    }
	}
	else
	{
	    if (1.0f - ts_position_on_tile.y >= ts_position_on_tile.x)
	    {
                ret = get_height_with_offset(vector2_t(0.0f, 1.0f),
                                             vector2_t(1.0f, 0.0f),
                                             vector2_t(0.0f, 0.0f),
                                             vector2_t(1.0f, 1.0f));
	    }
	    else
	    {
                ret = get_height_with_offset(vector2_t(0.0f, 1.0f),
                                             vector2_t(1.0f, 1.0f),
                                             vector2_t(1.0f, 0.0f),
                                             vector2_t(0.0f, 0.0f));
	    }
	}
    }
    else
    {
	if (ts_tile_corner_position.y % 2 == 0)
	{
	    if (1.0f - ts_position_on_tile.y >= ts_position_on_tile.x)
	    {
                ret = get_height_with_offset(vector2_t(0.0f, 1.0f),
                                             vector2_t(1.0f, 0.0f),
                                             vector2_t(0.0f, 0.0f),
                                             vector2_t(1.0f, 1.0f));
	    }
	    else
	    {
		ret = get_height_with_offset(vector2_t(0.0f, 1.0f),
                                             vector2_t(1.0f, 1.0f),
                                             vector2_t(1.0f, 0.0f),
                                             vector2_t(0.0f, 0.0f));
	    }
	}
	else
	{
	    if (ts_position_on_tile.y >= ts_position_on_tile.x)
	    {
                ret = get_height_with_offset(vector2_t(0.0f, 0.0f),
                                             vector2_t(0.0f, 1.0f),
                                             vector2_t(1.0f, 1.0f),
                                             vector2_t(1.0f, 0.0f));
	    }
	    else
	    {
                ret = get_height_with_offset(vector2_t(0.0f, 0.0f),
                                             vector2_t(1.0f, 1.0f),
                                             vector2_t(1.0f, 0.0f),
                                             vector2_t(0.0f, 1.0f));
	    }
	}
    }
    
    // result of the terrain collision in terrain space
    vector3_t ts_at (ts_p_xz.x, ret.ts_height, ts_p_xz.y);

    matrix4_t ts_to_ws = compute_ts_to_ws_matrix(t);
    
    vector3_t ws_at = vector3_t(ts_to_ws * vector4_t(ts_at, 1.0f));
    normal = glm::normalize(vector3_t(ts_to_ws * vector4_t(normal, 0.0f)));
    
    if (ts_p.y < 0.1f + ret.ts_height)
    {
        ret.triangle_exists = true;
        ret.ws_exact_pointed_at = ws_at;
        // Convert to world space
        ret.ws_triangle_position[0] = vector3_t(ts_to_ws * vector4_t(ret.ws_triangle_position[0], 1.0f));
        ret.ws_triangle_position[1] = vector3_t(ts_to_ws * vector4_t(ret.ws_triangle_position[1], 1.0f));
        ret.ws_triangle_position[2] = vector3_t(ts_to_ws * vector4_t(ret.ws_triangle_position[2], 1.0f));
        return(ret);
    }

    return {false};
}

internal terrain_triangle_t
get_triangle_pointing_at(vector3_t ws_ray_p,
                         const vector3_t &ws_ray_d,
                         morphable_terrain_t *t,
                         float32_t dt,
                         gpu_t *gpu)
{
    persist constexpr float32_t MAX_DISTANCE = 6.0f;
    persist constexpr float32_t MAX_DISTANCE_SQUARED = MAX_DISTANCE * MAX_DISTANCE;
    persist constexpr float32_t STEP_SIZE = 0.3f;

    matrix4_t ws_to_ts = t->inverse_transform;
    vector3_t ts_ray_p_start = vector3_t(ws_to_ts * vector4_t(ws_ray_p, 1.0f));
    vector3_t ts_ray_d = glm::normalize(vector3_t(ws_to_ts * vector4_t(ws_ray_d, 0.0f)));
    vector3_t ts_ray_diff = STEP_SIZE * ts_ray_d;

    ivector2_t ts_position = ivector2_t(-1);

    for (vector3_t ts_ray_step = ts_ray_d;
         distance_squared(ts_ray_step) < MAX_DISTANCE_SQUARED;
         ts_ray_step += ts_ray_diff)
    {
	vector3_t ts_ray_current_p = ts_ray_step + ts_ray_p_start;

        // If the ray is even on the terrain
	if (ts_ray_current_p.x >= 0.0f && ts_ray_current_p.x < (float32_t)t->xz_dim.x + 0.000001f
	    && ts_ray_current_p.z >= 0.0f && ts_ray_current_p.z < (float32_t)t->xz_dim.y + 0.000001f)
	{
            terrain_triangle_t triangle = get_triangle_from_pos(ts_ray_current_p, t);
            if (triangle.triangle_exists)
            {
                return(triangle);
            }
	}
    }

    return(terrain_triangle_t{false});
}

internal ivector2_t
get_coord_pointing_at(vector3_t ws_ray_p,
                      const vector3_t &ws_ray_d,
                      morphable_terrain_t *t,
                      float32_t dt,
                      gpu_t *gpu)
{
    persist constexpr float32_t MAX_DISTANCE = 6.0f;
    persist constexpr float32_t MAX_DISTANCE_SQUARED = MAX_DISTANCE * MAX_DISTANCE;
    persist constexpr float32_t STEP_SIZE = 0.3f;

    matrix4_t ws_to_ts = t->inverse_transform;
    vector3_t ts_ray_p_start = vector3_t(ws_to_ts * vector4_t(ws_ray_p, 1.0f));
    vector3_t ts_ray_d = glm::normalize(vector3_t(ws_to_ts * vector4_t(ws_ray_d, 0.0f)));
    vector3_t ts_ray_diff = STEP_SIZE * ts_ray_d;

    ivector2_t ts_position = ivector2_t(-1);

    for (vector3_t ts_ray_step = ts_ray_d;
         distance_squared(ts_ray_step) < MAX_DISTANCE_SQUARED;
         ts_ray_step += ts_ray_diff)
    {
	vector3_t ts_ray_current_p = ts_ray_step + ts_ray_p_start;

	if (ts_ray_current_p.x >= 0.0f && ts_ray_current_p.x < (float32_t)t->xz_dim.x + 0.000001f
	    && ts_ray_current_p.z >= 0.0f && ts_ray_current_p.z < (float32_t)t->xz_dim.y + 0.000001f)
	{
	    uint32_t x = (uint32_t)glm::round(ts_ray_current_p.x / 2.0f) * 2;
	    uint32_t z = (uint32_t)glm::round(ts_ray_current_p.z / 2.0f) * 2;

	    uint32_t index = get_terrain_index(x, z, t->xz_dim.x);

	    float32_t *heights_ptr = (float32_t *)t->heights;
	    if (ts_ray_current_p.y < heights_ptr[index])
	    {
		// ---- hit terrain at this point ----
		ts_position = ivector2_t(x, z);
		break;
	    }
	}
    }

    return(ts_position);
}

struct hitbox_t
{
    // Relative to the size of the entity
    // These are the values of when the entity size = 1
    float32_t x_max, x_min;
    float32_t y_max, y_min;
    float32_t z_max, z_min;
};

struct detected_collision_return_t
{
    bool detected;
    vector3_t ws_at;
    vector3_t ws_normal;
};

internal detected_collision_return_t
detect_terrain_collision(hitbox_t *hitbox,
                         const vector3_t &size,
                         const vector3_t &ws_p,
                         morphable_terrain_t *t)
{
    matrix4_t ws_to_ts = t->inverse_transform;

    // TODO: Make this more accurate (this is just for testing purposes at the moment)
    //    vector3_t contact_point = ws_p + vector3_t(0.0f, hitbox->y_min, 0.0f) * size;

    vector3_t ts_entity_height_offset = vector3_t( glm::scale(1.0f / t->size) * vector4_t(vector3_t(0.0f, hitbox->y_min, 0.0f) * size, 1.0f) );
    vector3_t ts_p = vector3_t(ws_to_ts * vector4_t(ws_p, 1.0f)) + ts_entity_height_offset;

    vector2_t ts_p_xz = vector2_t(ts_p.x, ts_p.z);

    // is outside the terrain
    if (ts_p_xz.x < 0.0f || ts_p_xz.x > t->xz_dim.x
        ||  ts_p_xz.y < 0.0f || ts_p_xz.y > t->xz_dim.y)
    {
	return {false};
    }

    // position of the player on one tile (square - two triangles)
    vector2_t ts_position_on_tile = vector2_t(ts_p_xz.x - glm::floor(ts_p_xz.x)
                                , ts_p_xz.y - glm::floor(ts_p_xz.y));

    // starting from (0, 0)
    ivector2_t ts_tile_corner_position = ivector2_t(glm::floor(ts_p_xz));


    // wrong math probably
    auto get_height_with_offset = [&t, ts_tile_corner_position, ts_position_on_tile](const vector2_t &offset_a,
										     const vector2_t &offset_b,
										     const vector2_t &offset_c,
                                                                                     vector3_t &normal) -> float32_t
	{
	    float32_t tl_x = ts_tile_corner_position.x;
	    float32_t tl_z = ts_tile_corner_position.y;
	
	    uint32_t triangle_indices[3] =
	    {
		get_terrain_index(offset_a.x + tl_x, offset_a.y + tl_z, t->xz_dim.x)
		, get_terrain_index(offset_b.x + tl_x, offset_b.y + tl_z, t->xz_dim.x)
		, get_terrain_index(offset_c.x + tl_x, offset_c.y + tl_z, t->xz_dim.x)
	    };

	    float32_t *terrain_heights = (float32_t *)t->heights;
	    vector3_t a = vector3_t(offset_a.x, terrain_heights[triangle_indices[0]], offset_a.y);
	    vector3_t b = vector3_t(offset_b.x, terrain_heights[triangle_indices[1]], offset_b.y);
	    vector3_t c = vector3_t(offset_c.x, terrain_heights[triangle_indices[2]], offset_c.y);

            normal = glm::normalize(glm::cross(glm::normalize(a - c), glm::normalize(b - c)));
            
	    return(barry_centric(a, b, c, ts_position_on_tile));
	};
    
    float32_t ts_height;

    vector3_t normal;

    if (ts_tile_corner_position.x % 2 == 0)
    {
	if (ts_tile_corner_position.y % 2 == 0)
	{
	    if (ts_position_on_tile.y >= ts_position_on_tile.x)
	    {
		ts_height = get_height_with_offset(vector2_t(0.0f, 0.0f),
                                                   vector2_t(0.0f, 1.0f),
                                                   vector2_t(1.0f, 1.0f),
                                                   normal);
	    }
	    else
	    {
		ts_height = get_height_with_offset(vector2_t(0.0f, 0.0f),
                                                   vector2_t(1.0f, 1.0f),
                                                   vector2_t(1.0f, 0.0f),
                                                   normal);
	    }
	}
	else
	{
	    if (1.0f - ts_position_on_tile.y >= ts_position_on_tile.x)
	    {
		ts_height = get_height_with_offset(vector2_t(0.0f, 1.0f),
                                                   vector2_t(1.0f, 0.0f),
                                                   vector2_t(0.0f, 0.0f),
                                                   normal);
	    }
	    else
	    {
		ts_height = get_height_with_offset(vector2_t(0.0f, 1.0f),
                                                   vector2_t(1.0f, 1.0f),
                                                   vector2_t(1.0f, 0.0f),
                                                   normal);
	    }
	}
    }
    else
    {
	if (ts_tile_corner_position.y % 2 == 0)
	{
	    if (1.0f - ts_position_on_tile.y >= ts_position_on_tile.x)
	    {
		ts_height = get_height_with_offset(vector2_t(0.0f, 1.0f),
                                                   vector2_t(1.0f, 0.0f),
                                                   vector2_t(0.0f, 0.0f),
                                                   normal);
	    }
	    else
	    {
		ts_height = get_height_with_offset(vector2_t(0.0f, 1.0f),
                                                   vector2_t(1.0f, 1.0f),
                                                   vector2_t(1.0f, 0.0f),
                                                   normal);
	    }
	}
	else
	{
	    if (ts_position_on_tile.y >= ts_position_on_tile.x)
	    {
		ts_height = get_height_with_offset(vector2_t(0.0f, 0.0f),
                                                   vector2_t(0.0f, 1.0f),
                                                   vector2_t(1.0f, 1.0f),
                                                   normal);
	    }
	    else
	    {
		ts_height = get_height_with_offset(vector2_t(0.0f, 0.0f),
                                                   vector2_t(1.0f, 1.0f),
                                                   vector2_t(1.0f, 0.0f),
                                                   normal);
	    }
	}
    }
    
    // result of the terrain collision in terrain space
    vector3_t ts_at (ts_p_xz.x, ts_height, ts_p_xz.y);

    vector3_t ws_at = vector3_t(compute_ts_to_ws_matrix(t) * vector4_t(ts_at, 1.0f));
    normal = glm::normalize(vector3_t(compute_ts_to_ws_matrix(t) * vector4_t(normal, 0.0f)));
    
    if (ts_p.y < 0.1f + ts_height)
    {
	return {true, ws_at, normal};
    }

    return {false};
}

internal vector3_t
get_sliding_down_direction(const vector3_t &ws_view_direction,
                           const vector3_t &ws_up_vector,
                           const vector3_t &ws_normal)
{
    vector3_t ws_right = glm::cross(ws_view_direction, ws_up_vector);
    vector3_t ws_down = glm::cross(ws_normal, ws_right);
    return(ws_down);
}

internal void
morph_terrain_at_triangle(terrain_triangle_t *triangle,
                           morphable_terrain_t *t,
                           float32_t morph_zone_radius,
                           float32_t dt,
                           gpu_t *gpu)
{
    uint32_t morph_quotients_radius_count = (morph_zone_radius) * (morph_zone_radius);

    struct morph_point_t
    {
        // In terrain space
        ivector2_t xz;
        // Probably like sin() or smoothstep or something to do with the distance from center
        float32_t quotient;
    } *morph_quotients = (morph_point_t *)allocate_linear(sizeof(morph_point_t) * morph_quotients_radius_count);

    morph_quotients_radius_count = 0;
    
    // Quarter of the mound
    // Dictated by the triangle positions
    struct mound_quarter_start_t
    {
        // Positive or negative ones only
        ivector2_t direction;
        // Terrain space
        ivector2_t coord;
    }  quarter_starts[4] = { mound_quarter_start_t{ triangle->offsets[0] * 2 - ivector2_t(1), get_ts_xz_coord_from_idx(triangle->idx[0], t) },
                             mound_quarter_start_t{ triangle->offsets[1] * 2 - ivector2_t(1), get_ts_xz_coord_from_idx(triangle->idx[1], t) },
                             mound_quarter_start_t{ triangle->offsets[2] * 2 - ivector2_t(1), get_ts_xz_coord_from_idx(triangle->idx[2], t) },
                             mound_quarter_start_t{ triangle->offsets[3] * 2 - ivector2_t(1), get_ts_xz_coord_from_idx(triangle->idx[3], t) }};
          
    float32_t *height_ptr = t->heights;

    for (uint32_t quarter = 0; quarter < 4; ++quarter)
    {
        for (uint32_t z = 0; z < morph_zone_radius; ++z)
        {
            for (uint32_t x = 0; x < morph_zone_radius; ++x)
            {
                vector2_t f32_coord = vector2_t((float32_t)x * quarter_starts[quarter].direction.x, (float32_t)z * quarter_starts[quarter].direction.y);
                float32_t squared_distance = distance_squared(f32_coord);
                if (squared_distance >= morph_zone_radius * morph_zone_radius
                    && abs(squared_distance - morph_zone_radius * morph_zone_radius) < 0.000001f)
                {
                    break;
                }

                // Morph the terrain
                int32_t ts_x_position = f32_coord.x + quarter_starts[quarter].coord.x;
                int32_t ts_z_position = f32_coord.y + quarter_starts[quarter].coord.y; // Y ==> Z
                int32_t index = get_terrain_index(ts_x_position, ts_z_position, t->xz_dim.x, t->xz_dim.y);

                // To create the mound shape (round)
                float32_t cos_theta = (cos((squared_distance / (morph_zone_radius * morph_zone_radius)) * 2.0f) + 1.0f) / 2.0f;
                cos_theta = cos_theta * cos_theta * cos_theta;

                if (index >= 0)
                {
                    height_ptr[index] += cos_theta * dt;
                }
            }
        }
    }
    
    t->is_modified = true;
}

internal void
morph_terrain_at(const ivector2_t &ts_position
		 , morphable_terrain_t *t
		 , float32_t morph_zone_radius
		 , float32_t dt
                 , gpu_t *gpu)
{
    uint32_t morph_quotients_outer_count = (morph_zone_radius - 1) * (morph_zone_radius - 1);
    uint32_t morph_quotients_inner_count = morph_zone_radius * 2 - 1;
    
    struct morph_point_t
    {
	ivector2_t xz;
	float32_t quotient;
    } *morph_quotients_outer_cache = (morph_point_t *)allocate_linear(sizeof(morph_point_t) * morph_quotients_outer_count)
	  , *morph_quotients_inner_cache = (morph_point_t *)allocate_linear(sizeof(morph_point_t) * morph_quotients_inner_count);

    morph_quotients_outer_count = morph_quotients_inner_count = 0;
    
    // ---- one quarter of the mound + prototype the mound modifier quotients for the rest of the 3/4 mounds ----
    for (int32_t z = 0; z < morph_zone_radius; ++z)
    {
	for (int32_t x = 0; x < morph_zone_radius; ++x)
	{
	    vector2_t f_coord = vector2_t((float32_t)x, (float32_t)z);
	    float32_t squared_d = distance_squared(f_coord);
	    if (squared_d >= morph_zone_radius * morph_zone_radius
		&& abs(squared_d - morph_zone_radius * morph_zone_radius) < 0.000001f) // <---- maybe don't check if d is equal...
	    {
		break;
	    }
	    // ---- Morph the terrain ----
	    int32_t ts_p_x = x + ts_position.x;
	    int32_t ts_p_z = z + ts_position.y;
	    
	    int32_t index = get_terrain_index(ts_p_x, ts_p_z, t->xz_dim.x, t->xz_dim.x);
	    
	    float32_t *p = (float32_t *)t->heights;
	    float32_t a = cos(squared_d / (morph_zone_radius * morph_zone_radius));
	    a = a * a * a;

	    if (index >= 0)
	    {
		p[index] += a * dt;
	    }

	    if (x == 0 || z == 0)
	    {
		morph_quotients_inner_cache[morph_quotients_inner_count++] = morph_point_t{ivector2_t(x, z), a};
	    }
	    else
	    {
		morph_quotients_outer_cache[morph_quotients_outer_count++] = morph_point_t{ivector2_t(x, z), a};
	    }
	}
    }

    // ---- do other half of the center cross ----
    for (uint32_t inner = 0; inner < morph_quotients_inner_count; ++inner)
    {
	int32_t x = -morph_quotients_inner_cache[inner].xz.x;
	int32_t z = -morph_quotients_inner_cache[inner].xz.y;

	if (x == 0 && z == 0) continue;

	// ---- morph the terrain ----
	int32_t ts_p_x = x + ts_position.x;
	int32_t ts_p_z = z + ts_position.y;
	    
	float32_t *p = (float32_t *)t->heights;
	
	int32_t index = get_terrain_index(ts_p_x, ts_p_z, t->xz_dim.x, t->xz_dim.x);
	if (index >= 0)
	{
	    p[index] += morph_quotients_inner_cache[inner].quotient * dt;
	}
    }

    // ---- do other 3/4 of the "outer" of the mound ----
    ivector2_t mound_quarter_multipliers[] { ivector2_t(+1, -1), ivector2_t(-1, -1), ivector2_t(-1, +1) };
    for (uint32_t m = 0; m < 3; ++m)
    {
	for (uint32_t outer = 0; outer < morph_quotients_outer_count; ++outer)
	{
	    int32_t x = morph_quotients_outer_cache[outer].xz.x * mound_quarter_multipliers[m].x;
	    int32_t z = morph_quotients_outer_cache[outer].xz.y * mound_quarter_multipliers[m].y;

	    // ---- morph the terrain ----
	    int32_t ts_p_x = x + ts_position.x;
	    int32_t ts_p_z = z + ts_position.y;
	    
	    int32_t index = get_terrain_index(ts_p_x, ts_p_z, t->xz_dim.x, t->xz_dim.x);
	    float32_t *p = (float32_t *)t->heights;

	    if (index >= 0)
	    {
		p[index] += morph_quotients_outer_cache[outer].quotient * dt;
	    }
	}
    }

    t->is_modified = true;
}

internal morphable_terrain_t *
on_which_terrain(const vector3_t &ws_position)
{
    struct distance_terrain_data_t { float32_t distance; morphable_terrain_t *terrain; };
    distance_terrain_data_t *distances = ALLOCA_T(distance_terrain_data_t, g_terrains.terrain_count);
    uint32_t distance_count = 0;
    
    for (uint32_t i = 0; i < g_terrains.terrain_count; ++i)
    {
        float32_t distance;
        if (is_on_terrain(ws_position, &g_terrains.terrains[i], &distance))
        {
            distances[distance_count].distance = distance;
            distances[distance_count].terrain = &g_terrains.terrains[i];
            ++distance_count;
        }
    }

    // Look for the smallest one
    distance_terrain_data_t *smallest = &distances[0];
    for (uint32_t i = 1; i < distance_count; ++i)
    {
        if (distances[i].distance < smallest->distance)
        {
            smallest = &distances[i];
        }
    }

    if (distance_count)
    {
        return(smallest->terrain);
    }
    else
    {
        return(nullptr);
    }
}

// ---- this command happens when rendering (terrain is updated on the cpu side at a different time) ----
internal void
update_terrain_on_gpu(gpu_command_queue_t *queue)
{
    for (uint32_t terrain = 0;
         terrain < g_terrains.terrain_count;
         ++terrain)
    {
        morphable_terrain_t *terrainptr = &g_terrains.terrains[terrain];
        if (terrainptr->is_modified)
        {
            update_gpu_buffer(&terrainptr->heights_gpu_buffer,
                              terrainptr->heights,
                              sizeof(float32_t) * terrainptr->xz_dim.x * terrainptr->xz_dim.y,
                              0,
                              VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
                              VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT,
                              &queue->q);
            terrainptr->is_modified = false;
        }
    }
}

internal float32_t
terrain_noise()
{
    float32_t r = (float32_t)(rand() % 100);
    r /= 500.0f;
    return(r);
}

internal void
make_3D_terrain_base(uint32_t width_x
		     , uint32_t depth_z
		     , float32_t random_displacement_factor
		     , gpu_buffer_t *mesh_xz_values
		     , gpu_buffer_t *idx_buffer
		     , model_t *model_info
		     , VkCommandPool *cmdpool
		     , gpu_t *gpu)
{
    assert(width_x & 0X1 && depth_z & 0X1);
    
    float32_t *vtx = (float32_t *)allocate_stack(sizeof(float32_t) * 2 * width_x * depth_z);
    uint32_t *idx = (uint32_t *)allocate_stack(sizeof(uint32_t) * 11 * (((width_x - 2) * (depth_z - 2)) / 4));
    
    for (uint32_t z = 0; z < depth_z; ++z)
    {
	for (uint32_t x = 0; x < width_x; ++x)
	{
	    // TODO: apply displacement factor to make terrain less perfect
	    uint32_t index = (x + depth_z * z) * 2;
	    vtx[index] = (float32_t)x;
	    vtx[index + 1] = (float32_t)z;
            /*if (x != 0 && z != 0 && x != width_x - 1 && z != depth_z - 1)
            {
                vtx[index] += terrain_noise();
                vtx[index + 1] += terrain_noise();
            }*/
	}	
    }

    uint32_t crnt_idx = 0;
    
    for (uint32_t z = 1; z < depth_z - 1; z += 2)
    {
        for (uint32_t x = 1; x < width_x - 1; x += 2)
	{
	    idx[crnt_idx++] = get_terrain_index(x, z, width_x);
	    idx[crnt_idx++] = get_terrain_index(x - 1, z - 1, width_x);
	    idx[crnt_idx++] = get_terrain_index(x - 1, z, width_x);
	    idx[crnt_idx++] = get_terrain_index(x - 1, z + 1, width_x);
	    idx[crnt_idx++] = get_terrain_index(x, z + 1, width_x);
	    idx[crnt_idx++] = get_terrain_index(x + 1, z + 1, width_x);
	    idx[crnt_idx++] = get_terrain_index(x + 1, z, width_x);
	    idx[crnt_idx++] = get_terrain_index(x + 1, z - 1, width_x);
	    idx[crnt_idx++] = get_terrain_index(x, z - 1, width_x);
	    idx[crnt_idx++] = get_terrain_index(x - 1, z - 1, width_x);
	    // ---- vulkan_t API special value for UINT32_T index type ----
	    idx[crnt_idx++] = 0XFFFFFFFF;
	}
    }
    
    // load data into buffers
    invoke_staging_buffer_for_device_local_buffer(memory_byte_buffer_t{sizeof(float32_t) * 2 * width_x * depth_z, vtx}
							  , VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT
							  , cmdpool
							  , mesh_xz_values
							  , gpu);

    invoke_staging_buffer_for_device_local_buffer(memory_byte_buffer_t{sizeof(uint32_t) * 11 * (((width_x - 1) * (depth_z - 1)) / 4), idx} // <--- this is idx, not vtx .... (stupid error)
							  , VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT
							  , cmdpool
							  , idx_buffer
							  , gpu);

    model_info->attribute_count = 2;
    model_info->attributes_buffer = (VkVertexInputAttributeDescription *)allocate_free_list(sizeof(VkVertexInputAttributeDescription) * model_info->attribute_count);
    model_info->binding_count = 2;
    model_info->bindings = (model_binding_t *)allocate_free_list(sizeof(model_binding_t) * model_info->binding_count);
    enum :uint32_t {GROUND_BASE_XY_VALUES_BND = 0, HEIGHT_BND = 1, GROUND_BASE_XY_VALUES_ATT = 0, HEIGHT_ATT = 1};
    // buffer that holds only the x-z values of each vertex - the reason is so that we can create multiple terrain meshes without copying the x-z values each time
    model_info->bindings[0].binding = 0;
    model_info->bindings[0].begin_attributes_creation(model_info->attributes_buffer);
    model_info->bindings[0].push_attribute(0, VK_FORMAT_R32G32_SFLOAT, sizeof(float32_t) * 2);
    model_info->bindings[0].end_attributes_creation();
    // buffer contains the y-values of each mesh and the colors of each mesh
    model_info->bindings[1].binding = 1;
    model_info->bindings[1].begin_attributes_creation(model_info->attributes_buffer);
    model_info->bindings[1].push_attribute(1, VK_FORMAT_R32_SFLOAT, sizeof(float32_t));
    model_info->bindings[1].end_attributes_creation();

    model_info->index_data.index_type = VK_INDEX_TYPE_UINT32;
    model_info->index_data.index_offset = 0; 
    model_info->index_data.index_count = 11 * (((width_x - 1) * (depth_z - 1)) / 4);
    model_info->index_data.index_buffer = idx_buffer->buffer;
    
    pop_stack();
    pop_stack();
}

internal void
make_3D_terrain_mesh_instance(uint32_t width_x
			      , uint32_t depth_z
			      , float32_t *&cpu_side_heights
			      , gpu_buffer_t *gpu_side_heights
			      , gpu_t *gpu)
{
    // don_t't need in the future
    cpu_side_heights = (float32_t *)allocate_free_list(sizeof(float32_t) * width_x * depth_z);
    memset(cpu_side_heights, 0, sizeof(float32_t) * width_x * depth_z);

    init_buffer(adjust_memory_size_for_gpu_alignment(sizeof(float32_t) * width_x * depth_z, gpu)
			, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT
			, VK_SHARING_MODE_EXCLUSIVE
			, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
			, gpu
			, gpu_side_heights);
}

internal void
make_terrain_mesh_data(uint32_t w, uint32_t d, morphable_terrain_t *terrain, gpu_t *gpu)
{
    make_3D_terrain_mesh_instance(w, d, terrain->heights, &terrain->heights_gpu_buffer, gpu);
    terrain->xz_dim = ivector2_t(w, d);
}


// TODO: make this take roughness and metalness to push to pushconstant or whatever 
internal void
make_terrain_rendering_data(morphable_terrain_t *terrain, gpu_material_submission_queue_t *queue
                            , const vector3_t &position, const quaternion_t &rotation, const vector3_t &size, const vector3_t &color)
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

    terrain->ws_n = vector3_t(glm::mat4_cast(terrain->gs_r) * vector4_t(0.0f, 1.0f, 0.0f, 0.0f));
}



internal void
make_terrain_instances(gpu_t *gpu, VkCommandPool *cmdpool)
{
    // Make the terrain render command recorder
    g_world_submission_queues[TERRAIN_QUEUE] = make_gpu_material_submission_queue(10
                                                                    , VK_SHADER_STAGE_VERTEX_BIT
                                                                    // Parameter is obsolete, must remove
                                                                    , VK_COMMAND_BUFFER_LEVEL_SECONDARY
                                                                    , cmdpool
                                                                    , gpu);

    auto *red_terrain = add_terrain();
    make_terrain_mesh_data(21, 21, red_terrain, gpu);
    make_terrain_rendering_data(red_terrain, &g_world_submission_queues[TERRAIN_QUEUE]
                                , vector3_t(0.0f, 0.0f, 200.0f)
                                , quaternion_t(glm::radians(vector3_t(30.0f, 20.0f, 0.0f)))
                                , vector3_t(15.0f)
                                , vector3_t(255.0f, 69.0f, 0.0f) / 256.0f);
    red_terrain->k_g = -8.5f;

    auto *green_terrain = add_terrain();
    make_terrain_mesh_data(21, 21, green_terrain, gpu);
    make_terrain_rendering_data(green_terrain, &g_world_submission_queues[TERRAIN_QUEUE]
                                , vector3_t(200.0f, 0.0f, 0.0f)
                                , quaternion_t(glm::radians(vector3_t(0.0f, 45.0f, 20.0f)))
                                , vector3_t(10.0f)
                                , vector3_t(0.1, 0.6, 0.2) * 0.7f);

    green_terrain->k_g = -8.5f;
}

internal void
add_staged_creation_terrains(gpu_t *gpu)
{
    for (uint32_t i = 0; i < g_terrains.create_count; ++i)
    {
        auto *create_staging_info = &g_terrains.create_stagings[i];
        auto *new_terrain = add_terrain();
        make_terrain_mesh_data(create_staging_info->dimensions, create_staging_info->dimensions, new_terrain, gpu);
        make_terrain_rendering_data(new_terrain, &g_world_submission_queues[TERRAIN_QUEUE]
                                    , create_staging_info->ws_p
                                    , quaternion_t(create_staging_info->rotation)
                                    , vector3_t(create_staging_info->size)
                                    , create_staging_info->color);
    }
    g_terrains.create_count = 0;
}

internal void
make_terrain_pointer(gpu_t *gpu, swapchain_t *swapchain)
{
    //    g_terrains.terrain_pointer.ppln = g_pipeline_manager.get_handle("pipeline.terrain_mesh_pointer_pipeline"_hash);

    g_terrains.terrain_pointer.ppln = g_pipeline_manager.add("pipeline.terrain_mesh_pointer"_hash);
    auto *terrain_pointer_ppln = g_pipeline_manager.get(g_terrains.terrain_pointer.ppln);
    {
        render_pass_handle_t dfr_render_pass = g_render_pass_manager.get_handle("render_pass.deferred_render_pass"_hash);
        shader_modules_t modules(shader_module_info_t{"shaders/SPV/terrain_pointer.vert.spv", VK_SHADER_STAGE_VERTEX_BIT},
                               shader_module_info_t{"shaders/SPV/terrain_pointer.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT});
        shader_uniform_layouts_t layouts(g_uniform_layout_manager.get_handle("uniform_layout.camera_transforms_ubo"_hash));
        shader_pk_data_t push_k = {200, 0, VK_SHADER_STAGE_VERTEX_BIT};
        shader_blend_states_t blending(false, false, false, false);
        dynamic_states_t dynamic(VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_LINE_WIDTH);
        make_graphics_pipeline(terrain_pointer_ppln, modules, VK_FALSE, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_POLYGON_MODE_LINE,
                               VK_CULL_MODE_NONE, layouts, push_k, get_backbuffer_resolution(), blending, nullptr,
                               true, 0.0f, dynamic, g_render_pass_manager.get(dfr_render_pass), 0, gpu);
    }
}

internal void
initialize_terrains(VkCommandPool *cmdpool
                    , swapchain_t *swapchain
                    , gpu_t *gpu)
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
        render_pass_handle_t dfr_render_pass = g_render_pass_manager.get_handle("render_pass.deferred_render_pass"_hash);
        shader_modules_t modules(shader_module_info_t{"shaders/SPV/terrain.vert.spv", VK_SHADER_STAGE_VERTEX_BIT},
                               shader_module_info_t{"shaders/SPV/terrain.geom.spv", VK_SHADER_STAGE_GEOMETRY_BIT},
                               shader_module_info_t{"shaders/SPV/terrain.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT});
        shader_uniform_layouts_t layouts(g_uniform_layout_manager.get_handle("uniform_layout.camera_transforms_ubo"_hash),
                                       g_uniform_layout_manager.get_handle("descriptor_set_layout.2D_sampler_layout"_hash));
        shader_pk_data_t push_k = {160, 0, VK_SHADER_STAGE_VERTEX_BIT};
        shader_blend_states_t blending(false, false, false, false);
        dynamic_states_t dynamic(VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_LINE_WIDTH);
        make_graphics_pipeline(terrain_ppln, modules, VK_TRUE, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN, VK_POLYGON_MODE_FILL,
                               VK_CULL_MODE_NONE, layouts, push_k, get_backbuffer_resolution(), blending, model_info,
                               true, 0.0f, dynamic, g_render_pass_manager.get(dfr_render_pass), 0, gpu);
    }

    g_terrains.terrain_shadow_ppln = g_pipeline_manager.add("pipeline.terrain_shadow"_hash);
    auto *terrain_shadow_ppln = g_pipeline_manager.get(g_terrains.terrain_shadow_ppln);
    {
        auto shadow_display = get_shadow_display();
        VkExtent2D shadow_extent = VkExtent2D{shadow_display.shadowmap_w, shadow_display.shadowmap_h};
        render_pass_handle_t shadow_render_pass = g_render_pass_manager.get_handle("render_pass.shadow_render_pass"_hash);
        shader_modules_t modules(shader_module_info_t{"shaders/SPV/terrain_shadow.vert.spv", VK_SHADER_STAGE_VERTEX_BIT},
                               shader_module_info_t{"shaders/SPV/terrain_shadow.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT});
        shader_uniform_layouts_t layouts(g_uniform_layout_manager.get_handle("uniform_layout.camera_transforms_ubo"_hash));
        shader_pk_data_t push_k = {160, 0, VK_SHADER_STAGE_VERTEX_BIT};
        shader_blend_states_t blending = {};
        dynamic_states_t dynamic(VK_DYNAMIC_STATE_DEPTH_BIAS, VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_LINE_WIDTH);
        make_graphics_pipeline(terrain_shadow_ppln, modules, VK_TRUE, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN, VK_POLYGON_MODE_FILL,
                               VK_CULL_MODE_NONE, layouts, push_k, shadow_extent, blending, model_info,
                               true, 0.0f, dynamic, g_render_pass_manager.get(shadow_render_pass), 0, gpu);
    }
    
    make_terrain_pointer(gpu, swapchain);
}

internal void
prepare_terrain_pointer_for_render(gpu_command_queue_t *queue
				   , VkDescriptorSet *ubo_set)
{
    // if the get_coord_pointing_at returns a coord with a negative - player is not pointing at the terrain
    //    if (g_terrains.terrain_pointer.ts_position.x >= 0)
    /*    {
	auto *ppln = g_pipeline_manager.get(g_terrains.terrain_pointer.ppln);
	command_buffer_bind_pipeline(ppln
					     , &queue->q);

	command_buffer_bind_descriptor_sets(ppln
						    , {1, ubo_set}
						    , &queue->q);

	struct
	{
	    matrix4_t ts_to_ws_terrain_model;
	    vector4_t color;
	    vector4_t ts_center_position;
	    // center first
	    float32_t ts_heights[8];
	} push_k;

	push_k.ts_to_ws_terrain_model = g_terrains.terrain_pointer.t->push_k.transform;
	push_k.color = vector4_t(1.0f);
	push_k.ts_center_position = vector4_t((float32_t)g_terrains.terrain_pointer.ts_position.x
					      , 0.0f
					      , (float32_t)g_terrains.terrain_pointer.ts_position.y
					      , 1.0f);

	uint32_t x = g_terrains.terrain_pointer.ts_position.x;
	uint32_t z = g_terrains.terrain_pointer.ts_position.y;
	uint32_t width = g_terrains.terrain_pointer.t->xz_dim.x;
	uint32_t depth = g_terrains.terrain_pointer.t->xz_dim.y;
	float32_t *heights = (float32_t *)(g_terrains.terrain_pointer.t->heights);

	auto calculate_height = [width, depth, heights](int32_t x, int32_t z) -> float32_t
	{
	    int32_t i = 0;
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
    // else don't render the pointer at all*/
}

// Is just a triangle
internal void
render_terrain_pointer(gpu_command_queue_t *queue,
                       uniform_group_t *ubo_transforms_group)
{
    if (g_terrains.terrain_pointer.triangle.triangle_exists)
    {
        vkCmdSetLineWidth(queue->q, 4.0f);
        
        auto *ppln = g_pipeline_manager.get(g_terrains.terrain_pointer.ppln);
        command_buffer_bind_pipeline(ppln, &queue->q);

	command_buffer_bind_descriptor_sets(ppln,
                                            {1, ubo_transforms_group},
                                            &queue->q);

	struct
	{
	    matrix4_t ts_to_ws_terrain_model;
	    vector4_t color;
	    vector4_t ts_center_position;
	    // center first
	    float32_t ts_heights[8];
            vector4_t new_pointer_system[3];
	} push_k;

	push_k.ts_to_ws_terrain_model = g_terrains.terrain_pointer.t->push_k.transform;
	push_k.color = vector4_t(1.0f);
        // The positions are already in world space (no need to push a world space matrix)
        push_k.new_pointer_system[0] = vector4_t(g_terrains.terrain_pointer.triangle.ws_triangle_position[0], 1.0f);
        push_k.new_pointer_system[1] = vector4_t(g_terrains.terrain_pointer.triangle.ws_triangle_position[1], 1.0f);
        push_k.new_pointer_system[2] = vector4_t(g_terrains.terrain_pointer.triangle.ws_triangle_position[2], 1.0f);
    
	command_buffer_push_constant(&push_k,
                                     sizeof(push_k),
                                     0,
                                     VK_SHADER_STAGE_VERTEX_BIT,
                                     ppln,
                                     &queue->q);

	command_buffer_draw(&queue->q,
                            3,
                            1,
                            0,
                            0);
    }
}

// TODO: Possibly remove the component system and just stick all the entity data into one big block of memory

using entity_handle_t = int32_t;

// Gravity acceleration on earth = 9.81 m/s^2
struct physics_component_t
{
    // Sum of mass of all particles
    float32_t mass = 1.0f; // KG
    // Sum ( all points of mass (vector quantity) * all masses of each point of mass ) / total body mass
    vector3_t center_of_gravity;
    // Depending on the shape of the body (see formula for rectangle if using hitbox, and for sphere if using bounding sphere)
    vector3_t moment_of_inertia;

    vector3_t acceleration;
    vector3_t velocity;
    vector3_t displacement;
    
    // F = ma
    vector3_t total_force_on_body;
    // G = mv
    vector3_t momentum;

    uint32_t entity_index;
    
    vector3_t gravity_accumulation = {};
    vector3_t friction_accumulation = {};
    vector3_t slide_accumulation = {};
    
    bool enabled;
    hitbox_t hitbox;
    vector3_t surface_normal;
    vector3_t surface_position;

    vector3_t force;
    
    // other forces (friction...)
};

struct camera_component_t
{
    uint32_t entity_index;
    
    // Can be set to -1, in that case, there is no camera bound
    camera_handle_t camera{-1};

    // Maybe some other variables to do with 3rd person / first person configs ...

    // Variable allows for smooth animation between up vectors when switching terrains
    bool in_animation = false;
    quaternion_t current_rotation;
};

struct input_component_t
{
    uint32_t entity_index;

    enum movement_flags_t { FORWARD, LEFT, BACK, RIGHT, DOWN };
    uint8_t movement_flags = 0;
};

struct rendering_component_t
{
    uint32_t entity_index;
    
    // push constant stuff for the graphics pipeline
    struct
    {
	// in world space
	matrix4_t ws_t{1.0f};
	vector4_t color;
    } push_k;

    bool enabled = true;
};

struct entity_body_t
{
    float32_t weight = 1.0f;
    hitbox_t hitbox;
};

struct entity_t
{
    entity_t(void) = default;
    
    constant_string_t id {""_hash};
    // position, direction, velocity
    // in above entity group space
    vector3_t ws_p{0.0f}, ws_d{0.0f}, ws_v{0.0f}, ws_input_v{0.0f};
    vector3_t ws_acceleration {0.0f};
    quaternion_t ws_r{0.0f, 0.0f, 0.0f, 0.0f};
    vector3_t size{1.0f};

    // For now is a pointer - is not a component because all entities will have one
    // This is the last terrain that the player was on / is still on
    // Is used for collision detection and also the camera view matrix (up vector...)
    struct morphable_terrain_t *on_t = nullptr;
    bool is_on_terrain = false;
    vector3_t surface_normal;
    vector3_t surface_position;

    static constexpr float32_t SWITCH_TERRAIN_ANIMATION_TIME = 0.6f;
    bool switch_terrain_animation_mode = false;
    quaternion_t previous_terrain_rot;
    quaternion_t current_rot;
    float32_t animation_time = 0.0f;

    //    struct entity_body_t body;
    
    struct components_t
    {

        int32_t camera_component;
        int32_t physics_component;
        int32_t input_component = -1;
        int32_t rendering_component;
        
    } components;
    
    entity_handle_t index;
};

struct dbg_entities_t
{
    bool hit_box_display = false;
    entity_t *render_sliding_vector_entity = nullptr;
};

global_var struct entities_t
{
    dbg_entities_t dbg;

    static constexpr uint32_t MAX_ENTITIES = 30;
    
    int32_t entity_count = {};
    entity_t entity_list[MAX_ENTITIES] = {};

    // All possible components: 
    int32_t physics_component_count = {};
    struct physics_component_t physics_components[MAX_ENTITIES] = {};

    int32_t camera_component_count = {};
    struct camera_component_t camera_components[MAX_ENTITIES] = {};

    int32_t input_component_count = {};
    struct input_component_t input_components[MAX_ENTITIES] = {};

    int32_t rendering_component_count = {};
    struct rendering_component_t rendering_components[MAX_ENTITIES] = {};

    struct hash_table_inline_t<entity_handle_t, 30, 5, 5> name_map{"map.entities"};

    pipeline_handle_t entity_ppln;
    pipeline_handle_t entity_shadow_ppln;
    pipeline_handle_t dbg_hitbox_ppln;

    model_handle_t entity_model;

    // For now:
    uint32_t main_entity;
    
    // have some sort of stack of REMOVED entities
} g_entities;

entity_t
construct_entity(const constant_string_t &name
		 //		 , Entity::Is_Group is_group
		 , vector3_t gs_p
		 , vector3_t ws_d
		 , quaternion_t gs_r)
{
    entity_t e;
    //    e.is_group = is_group;
    e.ws_p = gs_p;
    e.ws_d = ws_d;
    e.ws_r = gs_r;
    e.id = name;
    return(e);
}

internal entity_t *
get_entity(const constant_string_t &name)
{
    entity_handle_t v = *g_entities.name_map.get(name.hash);
    return(&g_entities.entity_list[v]);
}

internal entity_t *
get_entity(entity_handle_t v)
{
    return(&g_entities.entity_list[v]);
}

void
attach_camera_to_entity(entity_t *e
                        , int32_t camera_index)
{
    
}

internal struct camera_component_t *
add_camera_component(entity_t *e
                     , uint32_t camera_index)
{
    e->components.camera_component = g_entities.camera_component_count++;
    camera_component_t *component = &g_entities.camera_components[ e->components.camera_component ];
    component->entity_index = e->index;
    component->camera = camera_index;

    return(component);
}

internal void
update_camera_components(float32_t dt)
{
    for (uint32_t i = 0; i < g_entities.camera_component_count; ++i)
    {
        struct camera_component_t *component = &g_entities.camera_components[ i ];
        struct camera_t *camera = get_camera(component->camera);
        entity_t *e = &g_entities.entity_list[ component->entity_index ];

        vector3_t up = vector3_t(0.0f, 1.0f, 0.0f);
        if (e->on_t)
        {
            up = e->on_t->ws_n;
            if (e->switch_terrain_animation_mode)
            {
                up = vector3_t(glm::mat4_cast(e->current_rot) * vector4_t(0.0f, 1.0f, 0.0f, 1.0f));
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

internal struct rendering_component_t *
add_rendering_component(entity_t *e)
{
    e->components.rendering_component = g_entities.rendering_component_count++;
    rendering_component_t *component = &g_entities.rendering_components[ e->components.rendering_component ];
    component->entity_index = e->index;
    component->push_k = {};

    return(component);
}

internal void
update_rendering_component(float32_t dt)
{
    for (uint32_t i = 0; i < g_entities.rendering_component_count; ++i)
    {
        struct rendering_component_t *component = &g_entities.rendering_components[ i ];
        entity_t *e = &g_entities.entity_list[ component->entity_index ];

        if (component->enabled)
        {
            if (e->on_t)
            {
                component->push_k.ws_t = glm::translate(e->ws_p) * glm::mat4_cast(e->current_rot) * glm::scale(e->size);
            }
            else
            {
                component->push_k.ws_t = glm::translate(e->ws_p) * glm::scale(e->size);
            }
        }
        else
        {
            component->push_k.ws_t = matrix4_t(0.0f);
        }
    }
}

internal struct physics_component_t *
add_physics_component(entity_t *e
                      , bool enabled)
{
    e->components.physics_component = g_entities.physics_component_count++;
    struct physics_component_t *component = &g_entities.physics_components[ e->components.physics_component ];
    component->entity_index = e->index;
    component->enabled = enabled;

    return(component);
}

internal void
update_physics_components(float32_t dt)
{
    for (uint32_t i = 0; i < g_entities.physics_component_count; ++i)
    {
        struct physics_component_t *component = &g_entities.physics_components[ i ];
        entity_t *e = &g_entities.entity_list[ component->entity_index ];

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
                quaternion_t previous = quaternion_t(glm::radians(0.0f), vector3_t(0.0f, 0.0f, 0.0f));
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
        
        morphable_terrain_t *t = e->on_t;
        if (component->enabled)
        {
            /*
            // See if there was collision
            detected_collision_return_t collision = detect_terrain_collision(&component->hitbox, e->size, e->ws_p, e->on_t);
            component->surface_normal = collision.ws_normal;
            component->surface_position = collision.ws_at;

            // If collision with terrain was detected
            if (collision.detected)
            {
                component->gravity_accumulation = vector3_t(0.0f);
                
                // If input is enabled on the entity
                vector3_t ws_input_v = vector3_t(0.0f);
                if (e->components.input_component >= 0)
                {
                    vector3_t forward = get_sliding_down_direction(e->ws_d, e->on_t->ws_n, component->surface_normal);

                    float32_t speed = 40.0f;
                    
                    input_component_t *input = &g_entities.input_components[e->components.input_component];
                    if (input->movement_flags & (1 << input_component_t::movement_flags_t::FORWARD))
                    {
                        ws_input_v += forward;
                    }
                    if (input->movement_flags & (1 << input_component_t::movement_flags_t::LEFT))
                    {
                        ws_input_v += -glm::cross(forward, component->surface_normal);
                    }
                    if (input->movement_flags & (1 << input_component_t::movement_flags_t::BACK))
                    {
                        ws_input_v += -forward;
                    }
                    if (input->movement_flags & (1 << input_component_t::movement_flags_t::RIGHT))
                    {
                        ws_input_v += glm::cross(forward, component->surface_normal);
                    }

                    if (input->movement_flags & (1 << input_component_t::movement_flags_t::DOWN))
                    {
                        persist constexpr float32_t TEST_ROUGHNESS = 0.5f;
                        //                        component->friction_accumulation += -forward * TEST_ROUGHNESS * glm::dot(-component->surface_normal, -e->on_t->ws_n);
                        float32_t dot = glm::dot(-component->surface_normal, -e->on_t->ws_n);
                        float32_t cross = glm::length(glm::cross(-component->surface_normal, -e->on_t->ws_n));
                        
                        vector3_t friction_d = -forward * TEST_ROUGHNESS * cross;
                        vector3_t slide_d = forward * TEST_ROUGHNESS * dot;
                        component->slide_accumulation += friction_d + slide_d;
                        input->movement_flags = 0;
                    }
                    else
                    {
                        //                        component->friction_accumulation = vector3_t(0.0f);
                    }
                    
                    if (input->movement_flags)
                    {
                        ws_input_v = glm::normalize(ws_input_v) * speed;
                        input->movement_flags = 0;
                    }
                }

                //                e->ws_v += ws_input_v;

                vector3_t height_offset = vector3_t(glm::mat4_cast(e->on_t->gs_r) * vector4_t(-vector3_t(0.0f, component->hitbox.y_min, 0.0f) * e->size, 0.0f));

                e->ws_p = collision.ws_at + height_offset; //- vector3_t(0.0f, component->hitbox.y_min, 0.0f) * e->size + ws_input_v * dt
                e->ws_v += ws_input_v;
            }
            else
            {
                component->gravity_accumulation += t->k_g * e->on_t->ws_n;
            }

            e->ws_v += component->gravity_accumulation + component->slide_accumulation;

            e->ws_p += e->ws_v * dt;
            e->ws_v = vector3_t(0.0f);*/
        }
        else
        {
            e->ws_p += e->ws_input_v * dt;
        }

        if (e->animation_time > entity_t::SWITCH_TERRAIN_ANIMATION_TIME)
        {
            e->switch_terrain_animation_mode = false;
        }
        
        if (e->switch_terrain_animation_mode && e->on_t)
        {
            e->animation_time += dt;
            e->current_rot = glm::mix(e->previous_terrain_rot, e->on_t->gs_r, e->animation_time / entity_t::SWITCH_TERRAIN_ANIMATION_TIME);
        }
        else
        {
            e->current_rot = e->on_t->gs_r;
        }
        e->ws_acceleration = vector3_t(0.0f);
    }
}

internal input_component_t *
add_input_component(entity_t *e)
{
    e->components.input_component = g_entities.input_component_count++;
    input_component_t *component = &g_entities.input_components[ e->components.input_component ];
    component->entity_index = e->index;

    return(component);
}

// Don't even know yet if this is needed ? Only one entity will have this component - maybe just keep for consistency with the system
internal void
update_input_components(window_data_t *window
                        , float32_t dt
                        , gpu_t *gpu)
{
    // TODO: organise input handling better to take into account console being opened
    if (!console_is_receiving_input())
    {
        for (uint32_t i = 0; i < g_entities.input_component_count; ++i)
        {
            input_component_t *component = &g_entities.input_components[i];
            entity_t *e = &g_entities.entity_list[component->entity_index];
            physics_component_t *e_physics = &g_entities.physics_components[e->components.physics_component];

            vector3_t up = e->on_t->ws_n;
        
            // Mouse movement
            if (window->m_moved)
            {
                // TODO: Make sensitivity configurable with a file or something, and later menu
                persist constexpr uint32_t SENSITIVITY = 15.0f;
    
                vector2_t prev_mp = vector2_t(window->prev_m_x, window->prev_m_y);
                vector2_t curr_mp = vector2_t(window->m_x, window->m_y);

                vector3_t res = e->ws_d;
	    
                vector2_t d = (curr_mp - prev_mp);

                float32_t x_angle = glm::radians(-d.x) * SENSITIVITY * dt;// *elapsed;
                float32_t y_angle = glm::radians(-d.y) * SENSITIVITY * dt;// *elapsed;
                res = matrix3_t(glm::rotate(x_angle, up)) * res;
                vector3_t rotate_y = glm::cross(res, up);
                res = matrix3_t(glm::rotate(y_angle, rotate_y)) * res;

                e->ws_d = res;
            }

            // Mouse input
            ivector2_t ts_coord = get_coord_pointing_at(e->ws_p
                                                        , e->ws_d
                                                        , e->on_t
                                                        , dt
                                                        , gpu);
            g_terrains.terrain_pointer.triangle = get_triangle_pointing_at(e->ws_p, e->ws_d, e->on_t, dt, gpu);
            //            g_terrains.terrain_pointer.ts_position = ts_coord;
            g_terrains.terrain_pointer.t = e->on_t;
    
            // ---- modify the terrain ----
            if (window->mb_map[GLFW_MOUSE_BUTTON_RIGHT])
            {
                if (ts_coord.x >= 0)
                {
                    morph_terrain_at_triangle(&g_terrains.terrain_pointer.triangle,
                                               e->on_t,
                                               3.0f,
                                               dt,
                                               gpu);
//                    morph_terrain_at(ts_coord, e->on_t, 3, dt, gpu);
                }
            }

            // Keyboard input for entity
            uint32_t movements = 0;
            float32_t accelerate = 1.0f;
    
            auto acc_v = [&movements, &accelerate](const vector3_t &d, vector3_t &dst){ ++movements; dst += d * accelerate; };

            vector3_t d = glm::normalize(vector3_t(e->ws_d.x
                                                   , e->ws_d.y
                                                   , e->ws_d.z));

            morphable_terrain_t *t = e->on_t;
            matrix4_t inverse = t->inverse_transform;
    
            vector3_t ts_d = inverse * vector4_t(d, 0.0f);
    
            ts_d.y = 0.0f;

            d = vector3_t(t->push_k.transform * vector4_t(ts_d, 0.0f));
            d = glm::normalize(d);
    
            vector3_t res = {};

            bool detected_collision = detect_terrain_collision(&e_physics->hitbox, e->size, e->ws_p, e->on_t).detected;
    
 //            if (detected_collision) e->ws_v = vector3_t(0.0f);
    
            //    if (window->key_map[GLFW_KEY_P]) std::cout << glm::to_string(world.user_camera.d) << std::endl;
            if (window->key_map[GLFW_KEY_R]) {accelerate = 10.0f;}
            if (window->key_map[GLFW_KEY_W]) {acc_v(d, res); component->movement_flags |= (1 << input_component_t::movement_flags_t::FORWARD);}
            if (window->key_map[GLFW_KEY_A]) {acc_v(-glm::cross(d, up), res);component->movement_flags |= (1 << input_component_t::movement_flags_t::LEFT);}
            if (window->key_map[GLFW_KEY_S]) {acc_v(-d, res); component->movement_flags |= (1 << input_component_t::movement_flags_t::BACK);} 
            if (window->key_map[GLFW_KEY_D]) {acc_v(glm::cross(d, up), res); component->movement_flags |= (1 << input_component_t::movement_flags_t::RIGHT);}
    
            if (window->key_map[GLFW_KEY_SPACE])
            {
                //                if (e_physics->enabled)
                //                {
                //if (detected_collision)
                //                    {
                        // give some velotity towards the up vector
                //                        e->ws_v += up * 100.0f;
                //                        e->ws_p += e->ws_v * dt;
                //                    }
                    //                }
                    //                else
                    //                {
                     acc_v(up, res);
                     //                }
            }
    
            if (window->key_map[GLFW_KEY_LEFT_SHIFT]) acc_v(-up, res);
            if (window->key_map[GLFW_KEY_LEFT_SHIFT]) component->movement_flags |= (1 << input_component_t::movement_flags_t::DOWN);

            if (movements > 0)
            {
                res = res * 15.0f;

                e->ws_input_v = res;
            }
            else
            {
                e->ws_input_v = vector3_t(0.0f);
            }
        }
    }
}

internal entity_handle_t
add_entity(const entity_t &e)

{
    entity_handle_t view;
    view = g_entities.entity_count;

    g_entities.name_map.insert(e.id.hash, view);
    
    g_entities.entity_list[g_entities.entity_count++] = e;

    auto e_ptr = get_entity(view);

    e_ptr->index = view;

    return(view);
}

internal void
push_entity_to_queue(entity_t *e_ptr // Needs a rendering component attached
                     , model_handle_t model_handle
                     , gpu_material_submission_queue_t *queue)
{
    // ---- adds an entity to the stack of renderables
    auto *model = g_model_manager.get(model_handle);

    rendering_component_t *component = &g_entities.rendering_components[ e_ptr->components.rendering_component ];
    
    queue->push_material(&component->push_k
			 , sizeof(component->push_k)
			 , model->raw_cache_for_rendering
			 , model->index_data
			 , init_draw_indexed_data_default(1, model->index_data.index_count));
}

internal void
make_entity_instanced_renderable(model_handle_t model_handle
				 , const constant_string_t &e_mtrl_name)
{
    // TODO(luc) : first need to add support for instance rendering in material renderers.
}

internal void
update_entities(window_data_t *window
                , float32_t dt
                , gpu_t *gpu)
{
    update_input_components(window, dt, gpu);
    update_physics_components(dt);
    update_camera_components(dt);
    update_rendering_component(dt);
}

internal void
initialize_entities(gpu_t *gpu, swapchain_t *swapchain, VkCommandPool *cmdpool, window_data_t *window)
{
    g_entities.entity_model = g_model_manager.get_handle("model.cube_model"_hash);
    
    g_entities.entity_ppln = g_pipeline_manager.add("pipeline.model"_hash);
    auto *entity_ppln = g_pipeline_manager.get(g_entities.entity_ppln);
    {
        model_handle_t cube_hdl = g_model_manager.get_handle("model.cube_model"_hash);
        render_pass_handle_t dfr_render_pass = g_render_pass_manager.get_handle("render_pass.deferred_render_pass"_hash);
        shader_modules_t modules(shader_module_info_t{"shaders/SPV/model.vert.spv", VK_SHADER_STAGE_VERTEX_BIT},
                               shader_module_info_t{"shaders/SPV/model.geom.spv", VK_SHADER_STAGE_GEOMETRY_BIT},
                               shader_module_info_t{"shaders/SPV/model.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT});
        shader_uniform_layouts_t layouts(g_uniform_layout_manager.get_handle("uniform_layout.camera_transforms_ubo"_hash),
                                       g_uniform_layout_manager.get_handle("descriptor_set_layout.2D_sampler_layout"_hash));
        shader_pk_data_t push_k = {160, 0, VK_SHADER_STAGE_VERTEX_BIT};
        shader_blend_states_t blending(false, false, false, false);
        dynamic_states_t dynamic(VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_LINE_WIDTH);
        make_graphics_pipeline(entity_ppln, modules, VK_FALSE, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_POLYGON_MODE_FILL,
                               VK_CULL_MODE_NONE, layouts, push_k, get_backbuffer_resolution(), blending, g_model_manager.get(cube_hdl),
                               true, 0.0f, dynamic, g_render_pass_manager.get(dfr_render_pass), 0, gpu);
    }

    g_entities.dbg_hitbox_ppln = g_pipeline_manager.add("pipeline.hitboxes"_hash);
    auto *hitbox_ppln = g_pipeline_manager.get(g_entities.dbg_hitbox_ppln);
    {
        render_pass_handle_t dfr_render_pass = g_render_pass_manager.get_handle("render_pass.deferred_render_pass"_hash);
        shader_modules_t modules(shader_module_info_t{"shaders/SPV/hitbox_render.vert.spv", VK_SHADER_STAGE_VERTEX_BIT},
                                 shader_module_info_t{"shaders/SPV/hitbox_render.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT});
        shader_uniform_layouts_t layouts(g_uniform_layout_manager.get_handle("uniform_layout.camera_transforms_ubo"_hash));
        shader_pk_data_t push_k = {240, 0, VK_SHADER_STAGE_VERTEX_BIT};
        shader_blend_states_t blending(false, false, false, false);
        dynamic_states_t dynamic(VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_LINE_WIDTH);
        make_graphics_pipeline(hitbox_ppln, modules, VK_FALSE, VK_PRIMITIVE_TOPOLOGY_LINE_LIST, VK_POLYGON_MODE_LINE,
                               VK_CULL_MODE_NONE, layouts, push_k, get_backbuffer_resolution(), blending, nullptr,
                               true, 0.0f, dynamic, g_render_pass_manager.get(dfr_render_pass), 0, gpu);
    }

    g_entities.entity_shadow_ppln = g_pipeline_manager.add("pipeline.model_shadow"_hash);
    auto *entity_shadow_ppln = g_pipeline_manager.get(g_entities.entity_shadow_ppln);
    {
        auto shadow_display = get_shadow_display();
        VkExtent2D shadow_extent {shadow_display.shadowmap_w, shadow_display.shadowmap_h};
        model_handle_t cube_hdl = g_model_manager.get_handle("model.cube_model"_hash);
        render_pass_handle_t shadow_render_pass = g_render_pass_manager.get_handle("render_pass.shadow_render_pass"_hash);
        shader_modules_t modules(shader_module_info_t{"shaders/SPV/model_shadow.vert.spv", VK_SHADER_STAGE_VERTEX_BIT},
                               shader_module_info_t{"shaders/SPV/model_shadow.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT});
        shader_uniform_layouts_t layouts(g_uniform_layout_manager.get_handle("uniform_layout.camera_transforms_ubo"_hash));
        shader_pk_data_t push_k = {160, 0, VK_SHADER_STAGE_VERTEX_BIT};
        shader_blend_states_t blending(false);
        dynamic_states_t dynamic(VK_DYNAMIC_STATE_DEPTH_BIAS, VK_DYNAMIC_STATE_VIEWPORT);
        make_graphics_pipeline(entity_shadow_ppln, modules, VK_FALSE, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_POLYGON_MODE_FILL,
                               VK_CULL_MODE_NONE, layouts, push_k, shadow_extent, blending, g_model_manager.get(cube_hdl),
                               true, 0.0f, dynamic, g_render_pass_manager.get(shadow_render_pass), 0, gpu);
    }

    g_world_submission_queues[ENTITY_QUEUE] = make_gpu_material_submission_queue(20
                                                                                 , VK_SHADER_STAGE_VERTEX_BIT
                                                                                 , VK_COMMAND_BUFFER_LEVEL_SECONDARY
                                                                                 , cmdpool
                                                                                 , gpu);

    entity_t e = construct_entity("entity.main"_hash
				, vector3_t(50.0f, 10.0f, 280.0f)
				, glm::normalize(vector3_t(1.0f, 0.0f, 1.0f))
				, quaternion_t(0, 0, 0, 0));

    e.size = vector3_t(10.0f);
    
    entity_handle_t ev = add_entity(e);
    auto *e_ptr = get_entity(ev);
    g_entities.main_entity = ev;

    e_ptr->on_t = on_which_terrain(e.ws_p);

    physics_component_t *physics = add_physics_component(e_ptr, true);
    physics->enabled = false;
    physics->hitbox.x_min = -1.001f;
    physics->hitbox.x_max = 1.001f;
    physics->hitbox.y_min = -2.001f;
    physics->hitbox.y_max = 1.001f;
    physics->hitbox.z_min = -1.001f;
    physics->hitbox.z_max = 1.001f;
    auto *camera_component_ptr = add_camera_component(e_ptr, add_camera(window, get_backbuffer_resolution()));
    add_input_component(e_ptr);

    bind_camera_to_3d_scene_output(camera_component_ptr->camera);
        
    // add rotating entity
    entity_t r = construct_entity("entity.blue"_hash
				, vector3_t(200.0f, -40.0f, 300.0f)
                                  , vector3_t(1.0f, 0.0f, -1.0f)
				, quaternion_t(glm::radians(45.0f), vector3_t(0.0f, 1.0f, 0.0f)));

    r.size = vector3_t(10.0f);

    entity_handle_t rv = add_entity(r);
    auto *r_ptr = get_entity(rv); 
    
    r_ptr->on_t = &g_terrains.terrains[0];
    r_ptr->ws_d = vector3_t(glm::mat4_cast(g_terrains.terrains[0].gs_r) * vector4_t(r_ptr->ws_d, 0.0f));

    rendering_component_t *r_ptr_rendering = add_rendering_component(r_ptr);
    physics = add_physics_component(r_ptr, true);
    physics->hitbox.x_min = -1.001f;
    physics->hitbox.x_max = 1.001f;
    physics->hitbox.y_min = -1.001f;
    physics->hitbox.y_max = 1.001f;
    physics->hitbox.z_min = -1.001f;
    physics->hitbox.z_max = 1.001f;
    
    r_ptr_rendering->push_k.color = vector4_t(0.2f, 0.2f, 0.8f, 1.0f);
    
    push_entity_to_queue(r_ptr
                         , g_model_manager.get_handle("model.cube_model"_hash)
                         , &g_world_submission_queues[ENTITY_QUEUE]);

    entity_t r2 = construct_entity("entity.purple"_hash
				 , vector3_t(250.0f, -40.0f, 350.0f)
				 , vector3_t(0.0f)
				 , quaternion_t(glm::radians(45.0f), vector3_t(0.0f, 1.0f, 0.0f)));

    r2.size = vector3_t(10.0f);
    entity_handle_t rv2 = add_entity(r2);
    auto *r2_ptr = get_entity(rv2);

    rendering_component_t *r2_ptr_rendering = add_rendering_component(r2_ptr);
    add_physics_component(r2_ptr, false);

    physics = add_physics_component(r2_ptr, false);
    physics->hitbox.x_min = -1.001f;
    physics->hitbox.x_max = 1.001f;
    physics->hitbox.y_min = -1.001f;
    physics->hitbox.y_max = 1.001f;
    physics->hitbox.z_min = -1.001f;
    physics->hitbox.z_max = 1.001f;
    
    r2_ptr_rendering->push_k.color = vector4_t(0.6f, 0.0f, 0.6f, 1.0f);
    r2_ptr->on_t = &g_terrains.terrains[0];

    push_entity_to_queue(r2_ptr
                         , g_model_manager.get_handle("model.cube_model"_hash)
                         , &g_world_submission_queues[ENTITY_QUEUE]);
}

// ---- rendering of the entire world happens here ----
internal void
prepare_terrain_pointer_for_render(VkCommandBuffer *cmdbuf, VkDescriptorSet *set, framebuffer_t *fbo);

internal void
dbg_render_hitboxes(uniform_group_t *transforms_ubo, gpu_command_queue_t *queue)
{
    if (g_entities.dbg.hit_box_display)
    {
        auto *dbg_hitbox_ppln = g_pipeline_manager.get(g_entities.dbg_hitbox_ppln);
        command_buffer_bind_pipeline(dbg_hitbox_ppln, &queue->q);

        command_buffer_bind_descriptor_sets(dbg_hitbox_ppln, {1, transforms_ubo}, &queue->q);

        for (uint32_t i = 0; i < g_entities.physics_component_count; ++i)
        {
            struct push_k_t
            {
                alignas(16) matrix4_t model_matrix;
                alignas(16) vector4_t positions[8];
                alignas(16) vector4_t color;
            } pk;
            physics_component_t *physics_component = &g_entities.physics_components[i];
            entity_t *entity = get_entity(physics_component->entity_index);

            if (entity->index != g_entities.main_entity)
            {
                if (entity->on_t)
                {
                    pk.model_matrix = glm::translate(entity->ws_p) * glm::mat4_cast(entity->current_rot) * glm::scale(entity->size);
                }
                else
                {
                    pk.model_matrix = glm::translate(entity->ws_p) * glm::scale(entity->size);
                }

                hitbox_t *hit = &physics_component->hitbox;
                pk.positions[0] = vector4_t(hit->x_min, hit->y_min, hit->z_min, 1.0f);
                pk.positions[1] = vector4_t(hit->x_min, hit->y_max, hit->z_min, 1.0f);
                pk.positions[2] = vector4_t(hit->x_min, hit->y_max, hit->z_max, 1.0f);
                pk.positions[3] = vector4_t(hit->x_min, hit->y_min, hit->z_max, 1.0f);

                pk.positions[4] = vector4_t(hit->x_max, hit->y_min, hit->z_min, 1.0f);
                pk.positions[5] = vector4_t(hit->x_max, hit->y_max, hit->z_min, 1.0f);
                pk.positions[6] = vector4_t(hit->x_max, hit->y_max, hit->z_max, 1.0f);
                pk.positions[7] = vector4_t(hit->x_max, hit->y_min, hit->z_max, 1.0f);

                pk.color = vector4_t(1.0f, 0.0f, 0.0f, 1.0f);

                command_buffer_push_constant(&pk,
                                             sizeof(pk),
                                             0,
                                             VK_SHADER_STAGE_VERTEX_BIT,
                                             dbg_hitbox_ppln,
                                             &queue->q);

                command_buffer_draw(&queue->q, 24, 1, 0, 0);
            }
        }
    }
}

internal void
dbg_render_sliding_vectors(uniform_group_t *transforms_ubo, gpu_command_queue_t *queue)
{
    if (g_entities.dbg.render_sliding_vector_entity)
    {
        auto *dbg_hitbox_ppln = g_pipeline_manager.get(g_entities.dbg_hitbox_ppln);
        command_buffer_bind_pipeline(dbg_hitbox_ppln, &queue->q);

        command_buffer_bind_descriptor_sets(dbg_hitbox_ppln, {1, transforms_ubo}, &queue->q);

        struct push_k_t
        {
            alignas(16) matrix4_t model_matrix;
            alignas(16) vector4_t positions[8];
            alignas(16) vector4_t color;
        } pk;
        
        entity_t *entity = g_entities.dbg.render_sliding_vector_entity;
        physics_component_t *physics = &g_entities.physics_components[entity->components.physics_component];

        pk.model_matrix = glm::translate(physics->surface_position) * glm::scale(entity->size * 3.5f);

        pk.positions[0] = vector4_t(0.0f, 0.0f, 0.0f, 1.0f);
        pk.positions[1] = vector4_t(entity->ws_d, 1.0f);

        pk.color = vector4_t(0.0f, 1.0f, 0.0f, 1.0f);

        command_buffer_push_constant(&pk,
                                     sizeof(pk),
                                     0,
                                     VK_SHADER_STAGE_VERTEX_BIT,
                                     dbg_hitbox_ppln,
                                     &queue->q);

        command_buffer_draw(&queue->q, 2, 1, 0, 0);

        pk.positions[0] = vector4_t(0.0f, 0.0f, 0.0f, 1.0f);
        pk.positions[1] = vector4_t(physics->surface_normal, 1.0f);

        pk.color = vector4_t(0.0f, 0.0f, 1.0f, 1.0f);

        command_buffer_push_constant(&pk,
                                     sizeof(pk),
                                     0,
                                     VK_SHADER_STAGE_VERTEX_BIT,
                                     dbg_hitbox_ppln,
                                     &queue->q);

        command_buffer_draw(&queue->q, 2, 1, 0, 0);

        vector3_t down = get_sliding_down_direction(entity->ws_d, entity->on_t->ws_n, physics->surface_normal);
        pk.positions[0] = vector4_t(0.0f, 0.0f, 0.0f, 1.0f);
        pk.positions[1] = vector4_t(down, 1.0f);

        pk.color = vector4_t(1.0f, 1.0f, 0.0f, 1.0f);

        command_buffer_push_constant(&pk,
                                     sizeof(pk),
                                     0,
                                     VK_SHADER_STAGE_VERTEX_BIT,
                                     dbg_hitbox_ppln,
                                     &queue->q);

        command_buffer_draw(&queue->q, 2, 1, 0, 0);
    }
}

internal void
render_world(vulkan_state_t *vk
	     , uint32_t image_index
	     , uint32_t current_frame
	     , gpu_command_queue_t *queue)
{
    // Fetch some data needed to render
    auto transforms_ubo_uniform_groups = get_camera_transform_uniform_groups();
    shadow_display_t shadow_display_data = get_shadow_display();
    
    uniform_group_t uniform_groups[2] = {transforms_ubo_uniform_groups[image_index], shadow_display_data.texture};

    camera_t *camera = get_camera_bound_to_3d_output();

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

        //        prepare_terrain_pointer_for_render(queue, &transforms_ubo_uniform_groups[image_index]);
        render_terrain_pointer(queue, &transforms_ubo_uniform_groups[image_index]);

        render_3d_frustum_debug_information(queue, image_index);
        dbg_render_hitboxes(&uniform_groups[0], queue);
        dbg_render_sliding_vectors(&uniform_groups[0], queue);
        
        // ---- render skybox ----
        auto *cube_model = g_model_manager.get(g_entities.entity_model);
        render_atmosphere({1, uniform_groups}, camera->p, cube_model, queue);
    }
    end_deferred_rendering(camera->v_m, queue);

    apply_pfx_on_scene(queue, &transforms_ubo_uniform_groups[image_index], camera->v_m, camera->p_m, &vk->gpu);
}

internal int32_t
lua_get_player_position(lua_State *state);

internal int32_t
lua_set_player_position(lua_State *state);

internal int32_t
lua_spawn_terrain(lua_State *state);

internal int32_t
lua_toggle_collision_box_render(lua_State *state);

internal int32_t
lua_render_entity_direction_information(lua_State *state);

internal int32_t
lua_toggle_entity_model_display(lua_State *state);

internal int32_t
lua_set_veclocity_in_view_direction(lua_State *state);

internal int32_t
lua_get_player_ts_view_direction(lua_State *state);

internal int32_t
lua_print_player_terrain_position_info(lua_State *state);

void
initialize_world(window_data_t *window
                 , vulkan_state_t *vk
                 , VkCommandPool *cmdpool)
{
    add_global_to_lua(script_primitive_type_t::FUNCTION, "get_player_position", &lua_get_player_position);
    add_global_to_lua(script_primitive_type_t::FUNCTION, "set_player_position", &lua_set_player_position);
    add_global_to_lua(script_primitive_type_t::FUNCTION, "spawn_terrain", &lua_spawn_terrain);
    add_global_to_lua(script_primitive_type_t::FUNCTION, "toggle_hit_box_display", &lua_toggle_collision_box_render);
    add_global_to_lua(script_primitive_type_t::FUNCTION, "render_direction_info", &lua_render_entity_direction_information);
    add_global_to_lua(script_primitive_type_t::FUNCTION, "toggle_entity_model_display", &lua_toggle_entity_model_display);
    add_global_to_lua(script_primitive_type_t::FUNCTION, "set_velocity", &lua_toggle_entity_model_display);
    add_global_to_lua(script_primitive_type_t::FUNCTION, "get_ts_view_dir", &lua_get_player_ts_view_direction);
    add_global_to_lua(script_primitive_type_t::FUNCTION, "print_player_terrain_position_info", &lua_print_player_terrain_position_info);
    
    initialize_terrains(cmdpool, &vk->swapchain, &vk->gpu);
    initialize_entities(&vk->gpu, &vk->swapchain, cmdpool, window);
}

void
update_world(window_data_t *window
	     , vulkan_state_t *vk
	     , float32_t dt
	     , uint32_t image_index
	     , uint32_t current_frame
	     , gpu_command_queue_t *cmdbuf)
{
    handle_input_debug(window, dt, &vk->gpu);
    
    update_entities(window, dt, &vk->gpu);

    add_staged_creation_terrains(&vk->gpu);

    // ---- Actually rendering the frame ----
    update_3d_output_camera_transforms(image_index, &vk->gpu);
    
    render_world(vk, image_index, current_frame, cmdbuf);    
}


#include <glm/gtx/string_cast.hpp>


// Not to do with moving the entity, just debug stuff : will be used later for stuff like opening menus
void
handle_input_debug(window_data_t *window
                   , float32_t dt
                   , gpu_t *gpu)
{
    if (!console_is_receiving_input())
    {
        // ---- get bound entity ----
        // TODO make sure to check if main_entity < 0
        entity_t *e_ptr = &g_entities.entity_list[g_entities.main_entity];
        camera_component_t *e_camera_component = &g_entities.camera_components[e_ptr->components.camera_component];
        physics_component_t *e_physics = &g_entities.physics_components[e_ptr->components.physics_component];
        camera_t *e_camera = get_camera(e_camera_component->camera);
        vector3_t up = e_ptr->on_t->ws_n;
    
        shadow_matrices_t shadow_data = get_shadow_matrices();
        shadow_debug_t    shadow_debug = get_shadow_debug();
    
        //    shadow_data.light_view_matrix = glm::lookAt(vector3_t(0.0f), -glm::normalize(light_pos), vector3_t(0.0f, 1.0f, 0.0f));

        if (window->key_map[GLFW_KEY_P])
        {
            for (uint32_t i = 0; i < 8; ++i)
            {
                e_camera->captured_frustum_corners[i] = shadow_debug.frustum_corners[i];
            }

            e_camera->captured_shadow_corners[0] = vector4_t(shadow_debug.x_min, shadow_debug.y_max, shadow_debug.z_min, 1.0f);
            e_camera->captured_shadow_corners[1] = vector4_t(shadow_debug.x_max, shadow_debug.y_max, shadow_debug.z_min, 1.0f);
            e_camera->captured_shadow_corners[2] = vector4_t(shadow_debug.x_max, shadow_debug.y_min, shadow_debug.z_min, 1.0f);
            e_camera->captured_shadow_corners[3] = vector4_t(shadow_debug.x_min, shadow_debug.y_min, shadow_debug.z_min, 1.0f);

            e_camera->captured_shadow_corners[4] = vector4_t(shadow_debug.x_min, shadow_debug.y_max, shadow_debug.z_max, 1.0f);
            e_camera->captured_shadow_corners[5] = vector4_t(shadow_debug.x_max, shadow_debug.y_max, shadow_debug.z_max, 1.0f);
            e_camera->captured_shadow_corners[6] = vector4_t(shadow_debug.x_max, shadow_debug.y_min, shadow_debug.z_max, 1.0f);
            e_camera->captured_shadow_corners[7] = vector4_t(shadow_debug.x_min, shadow_debug.y_min, shadow_debug.z_max, 1.0f);
        }
    }
}



void
destroy_world(gpu_t *gpu)
{
    g_render_pass_manager.clean_up(gpu);
    g_image_manager.clean_up(gpu);
    g_framebuffer_manager.clean_up(gpu);
    g_pipeline_manager.clean_up(gpu);
    g_gpu_buffer_manager.clean_up(gpu);

    clean_up_terrain(gpu);

    for (uint32_t i = 0; i < g_uniform_layout_manager.count; ++i)
    {
	vkDestroyDescriptorSetLayout(gpu->logical_device, g_uniform_layout_manager.objects[i], nullptr);
    }

    destroy_graphics(gpu);
}

internal int32_t
lua_get_player_position(lua_State *state)
{
    // For now, just sets the main player's position
    entity_t *main_entity = &g_entities.entity_list[g_entities.main_entity];
    lua_pushnumber(state, main_entity->ws_p.x);
    lua_pushnumber(state, main_entity->ws_p.y);
    lua_pushnumber(state, main_entity->ws_p.z);
    return(3);
}

internal int32_t
lua_set_player_position(lua_State *state)
{
    float32_t x = lua_tonumber(state, -3);
    float32_t y = lua_tonumber(state, -2);
    float32_t z = lua_tonumber(state, -1);
    entity_t *main_entity = &g_entities.entity_list[g_entities.main_entity];
    main_entity->ws_p.x = x;
    main_entity->ws_p.y = y;
    main_entity->ws_p.z = z;
    return(0);
}

internal int32_t
lua_spawn_terrain(lua_State *state)
{
    uint32_t dimensions = lua_tonumber(state, -2);
    float32_t size = lua_tonumber(state, -1);
    
    entity_t *main_entity = &g_entities.entity_list[g_entities.main_entity];

    uint32_t xrotation = rand() % 90;
    uint32_t yrotation = rand() % 90;
    uint32_t zrotation = rand() % 90;
    
    auto *create = &g_terrains.create_stagings[g_terrains.create_count++];
    create->dimensions = dimensions;
    create->size = size;
    create->ws_p = main_entity->ws_p;
    create->rotation = glm::radians(vector3_t(xrotation, yrotation, zrotation));
    create->color = vector3_t(0.4f, 0.4f, 0.6f);

    return(0);
}

internal int32_t
lua_toggle_collision_box_render(lua_State *state)
{
    g_entities.dbg.hit_box_display ^= true;
    return(0);
}

internal int32_t
lua_render_entity_direction_information(lua_State *state)
{
    const char *name = lua_tostring(state, -1);
    constant_string_t kname = make_constant_string(name, strlen(name));

    g_entities.dbg.render_sliding_vector_entity = get_entity(kname);

    persist char buffer[50];
    sprintf(buffer, "rendering for entity: %s", name);
    console_out(buffer);
    
    return(0);
}

internal int32_t
lua_toggle_entity_model_display(lua_State *state)
{
    const char *name = lua_tostring(state, -1);
    constant_string_t kname = make_constant_string(name, strlen(name));

    entity_t *entity = get_entity(kname);

    g_entities.rendering_components[entity->components.rendering_component].enabled ^= true;

    return(0);
}

internal int32_t
lua_set_veclocity_in_view_direction(lua_State *state)
{
    const char *name = lua_tostring(state, -2);
    float32_t velocity = lua_tonumber(state, -1);
    constant_string_t kname = make_constant_string(name, strlen(name));
    entity_t *entity = get_entity(kname);
    entity->ws_v += entity->ws_d * velocity;
    return(0);
}

internal int32_t
lua_get_player_ts_view_direction(lua_State *state)
{
    // For now, just sets the main player's position
    entity_t *main_entity = &g_entities.entity_list[g_entities.main_entity];
    vector4_t dir = glm::scale(main_entity->on_t->size) * main_entity->on_t->inverse_transform * vector4_t(main_entity->ws_d, 0.0f);
    lua_pushnumber(state, dir.x);
    lua_pushnumber(state, dir.y);
    lua_pushnumber(state, dir.z);
    return(3);
}

internal int32_t
lua_print_player_terrain_position_info(lua_State *state)
{
    struct entity_t *main_entity = &g_entities.entity_list[g_entities.main_entity];

    vector3_t ts_p = vector3_t(main_entity->on_t->inverse_transform * vector4_t(main_entity->ws_p, 1.0f));
    struct morphable_terrain_t *t = main_entity->on_t;
    
    vector2_t ts_p_xz = vector2_t(ts_p.x, ts_p.z);

    // is outside the terrain
    if (ts_p_xz.x < 0.0f || ts_p_xz.x > t->xz_dim.x
        ||  ts_p_xz.y < 0.0f || ts_p_xz.y > t->xz_dim.y)
    {
	return {false};
    }

    // position of the player on one tile (square - two triangles)
    vector2_t ts_position_on_tile = vector2_t(ts_p_xz.x - glm::floor(ts_p_xz.x)
                                              , ts_p_xz.y - glm::floor(ts_p_xz.y));

    // starting from (0, 0)
    ivector2_t ts_tile_corner_position = ivector2_t(glm::floor(ts_p_xz));


    // wrong math probably
    auto get_height_with_offset = [&t, ts_tile_corner_position, ts_position_on_tile](const vector2_t &offset_a,
										     const vector2_t &offset_b,
										     const vector2_t &offset_c,
                                                                                     // For morphing function
                                                                                     const vector2_t &offset_d) -> terrain_triangle_t
	{
	    float32_t tl_x = ts_tile_corner_position.x;
	    float32_t tl_z = ts_tile_corner_position.y;
	
	    uint32_t triangle_indices[3] =
	    {
		get_terrain_index(offset_a.x + tl_x, offset_a.y + tl_z, t->xz_dim.x)
		, get_terrain_index(offset_b.x + tl_x, offset_b.y + tl_z, t->xz_dim.x)
		, get_terrain_index(offset_c.x + tl_x, offset_c.y + tl_z, t->xz_dim.x)
	    };

	    float32_t *terrain_heights = (float32_t *)t->heights;
	    vector3_t a = vector3_t(offset_a.x, terrain_heights[triangle_indices[0]], offset_a.y);
	    vector3_t b = vector3_t(offset_b.x, terrain_heights[triangle_indices[1]], offset_b.y);
	    vector3_t c = vector3_t(offset_c.x, terrain_heights[triangle_indices[2]], offset_c.y);

            terrain_triangle_t triangle = {};
            triangle.ts_height = barry_centric(a, b, c, ts_position_on_tile);
            triangle.idx[0] = triangle_indices[0];
            triangle.idx[1] = triangle_indices[1];
            triangle.idx[2] = triangle_indices[2];
            // For morphing function
            triangle.idx[3] = get_terrain_index(offset_d.x + tl_x, offset_d.y + tl_z, t->xz_dim.x);
            
            // For now still in terrain space, get converted later
            triangle.ws_triangle_position[0] = vector3_t(offset_a.x + tl_x, a.y, offset_a.y + tl_z);
            triangle.ws_triangle_position[1] = vector3_t(offset_b.x + tl_x, b.y, offset_b.y + tl_z);
            triangle.ws_triangle_position[2] = vector3_t(offset_c.x + tl_x, c.y, offset_c.y + tl_z);
            // For morphing function
            triangle.offsets[0] = ivector2_t(offset_a.x, offset_a.y);
            triangle.offsets[1] = ivector2_t(offset_b.x, offset_b.y);
            triangle.offsets[2] = ivector2_t(offset_c.x, offset_c.y);
            triangle.offsets[3] = ivector2_t(offset_d.x, offset_d.y);
	    return(triangle);
	};
    
    terrain_triangle_t ret = {};

    vector3_t normal;
    
    if (ts_tile_corner_position.x % 2 == 0)
    {
	if (ts_tile_corner_position.y % 2 == 0)
	{
	    if (ts_position_on_tile.y >= ts_position_on_tile.x)
	    {
                // 1
                console_out("1\n");
	    }
	    else
	    {
                // 2
                console_out("2\n");
	    }
	}
	else
	{
	    if (1.0f - ts_position_on_tile.y >= ts_position_on_tile.x)
	    {
                // 3
                console_out("3\n");
	    }
	    else
	    {
                // 4
                console_out("4\n");
	    }
	}
    }
    else
    {
	if (ts_tile_corner_position.y % 2 == 0)
	{
	    if (1.0f - ts_position_on_tile.y >= ts_position_on_tile.x)
	    {
                // 5
                console_out("5\n");
	    }
	    else
	    {
                // 6
                console_out("6\n");
	    }
	}
	else
	{
	    if (ts_position_on_tile.y >= ts_position_on_tile.x)
	    {
                // 7
                console_out("7\n");
	    }
	    else
	    {
                // 8
                console_out("8\n");
	    }
	}
    }

    return(0);
}
