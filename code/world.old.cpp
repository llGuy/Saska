/* world.cpp */
// TODO: DEFER ALL GPU RELATED OPERATIONS!!!
// TODO: Get rid of most of the glm::length calls in the physics calculations (terrain collision stuff)
// TODO: Have a startup script so that you can reload the game

#include "ui.hpp"
#include "script.hpp"
#include "world.hpp"
#include <glm/glm.hpp>
#include <glm/gtx/transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/projection.hpp>

#include "graphics.hpp"

#include "core.hpp"

#include "game.hpp"

#define MAX_ENTITIES_UNDER_TOP 10
#define MAX_ENTITIES_UNDER_PLANET 150

constexpr float32_t PI = 3.14159265359f;

// To initialize with initialize translation unit function
global_var gpu_material_submission_queue_t *g_world_submission_queues;
global_var struct morphable_terrains_t *g_terrains;
global_var struct entities_t *g_entities;
global_var struct network_world_state_t *g_network_world_state;

enum { TERRAIN_QUEUE, ENTITY_QUEUE, ROLLING_ENTITY_QUEUE };

enum matrix4_mul_vec3_with_translation_flag { WITH_TRANSLATION, WITHOUT_TRANSLATION, TRANSLATION_DONT_CARE };

vector3_t matrix4_mul_vec3(const matrix4_t &matrix, const vector3_t &vector, matrix4_mul_vec3_with_translation_flag flag)
{
    switch(flag)
    {
    case WITH_TRANSLATION: return vector3_t( matrix * vector4_t(vector, 1.0f) );
    case WITHOUT_TRANSLATION: case TRANSLATION_DONT_CARE: return vector3_t( matrix * vector4_t(vector, 0.0f) );
    default: assert(0); return vector3_t(0.0f);
    }
}

struct sphere_triangle_collision_return_t
{
    bool32_t collision_happened;
    bool32_t sphere_was_under_the_terrain; // This means, we need to adjust the sphere's position to be on top of the terrain
    vector3_t ts_new_sphere_position; // Only for in the case of the sphere being underneath the terrain
    
    float32_t es_distance;
    vector3_t es_sphere_contact_point;

    vector3_t ts_surface_normal_at_collision_point;

    bool32_t is_edge;
};

internal_function bool32_t is_point_in_triangle(const vector3_t &point, const vector3_t &tri_point_a, const vector3_t &tri_point_b, const vector3_t &tri_point_c)
{
    vector3_t cross11 = glm::cross((tri_point_c - tri_point_b), (point - tri_point_b));
    vector3_t cross12 = glm::cross((tri_point_c - tri_point_b), (tri_point_a - tri_point_b));
    float32_t d1 = glm::dot(cross11, cross12);
    if(d1 >= 0)
    {
        vector3_t cross21 = glm::cross((tri_point_c - tri_point_a), (point - tri_point_a));
        vector3_t cross22 = glm::cross((tri_point_c - tri_point_a), (tri_point_b - tri_point_a));
        float32_t d2 = glm::dot(cross21, cross22);
        if(d2 >= 0)
        {
            vector3_t cross31 = glm::cross((tri_point_b - tri_point_a), (point - tri_point_a));
            vector3_t cross32 = glm::cross((tri_point_b - tri_point_a), (tri_point_c - tri_point_a));
            float32_t d3 = glm::dot(cross31, cross32);
            if(d3 >= 0)
            {
                return 1;
            }
        }
    }
    return 0;
}

// This function solves the quadratic eqation "At^2 + Bt + C = 0" and is found in Kasper Fauerby's paper on collision detection and response
internal_function bool get_smallest_root(float32_t a, float32_t b, float32_t c, float32_t max_r, float32_t *root) 
{
    // Check if a solution exists
    float determinant = b * b - 4.0f * a *c;
    // If determinant is negative it means no solutions.
    if (determinant < 0.0f) return false;
    // calculate the two roots: (if determinant == 0 then
    // x1==x2 but lets disregard that slight optimization)
    float sqrt_d = sqrt(determinant);
    float r1 = (-b - sqrt_d) / (2 * a);
    float r2 = (-b + sqrt_d) / (2 * a);
    // Sort so x1 <= x2
    if (r1 > r2)
    {
        float32_t temp = r2;
        r2 = r1;
        r1 = temp;
    }
    // Get lowest root:
    if (r1 > 0 && r1 < max_r)
    {
        *root = r1;
        return true;
    }
    // It is possible that we want x2 - this can happen
    // if x1 < 0
    if (r2 > 0 && r2 < max_r)
    {
        *root = r2;
        return true;
    }

    // No (valid) solutions
    return false;
}

internal_function float32_t get_plane_constant(const vector3_t &plane_point, const vector3_t &plane_normal)
{
    return -( (plane_point.x * plane_normal.x) + (plane_point.y * plane_normal.y) + (plane_point.z * plane_normal.z) );
}

internal_function void make_triangle_collision_return(float32_t es_distance, const vector3_t &es_contact_point, sphere_triangle_collision_return_t *ret)
{
    ret->es_distance = es_distance;
    ret->es_sphere_contact_point = es_contact_point;
}

internal_function void check_collision_with_vertex(const vector3_t &es_sphere_velocity, const vector3_t &es_sphere_position, const vector3_t &es_vertex, const vector3_t &ts_surface_normal, sphere_triangle_collision_return_t *collision)
{
    float32_t a = distance_squared(es_sphere_velocity);
    float32_t b = 2.0f * glm::dot(es_sphere_velocity, es_sphere_position - es_vertex);
    float32_t c = distance_squared(es_vertex - es_sphere_position) - 1.0f;
            
    float32_t new_resting_instance;
    if (get_smallest_root(a, b, c, 1.0f, &new_resting_instance))
    {
        float32_t es_distance = glm::length(new_resting_instance * es_sphere_velocity);
                
        if (es_distance < collision->es_distance)
        {
            collision->is_edge = 1;
            collision->collision_happened = 1;
            collision->es_distance = es_distance;
            collision->es_sphere_contact_point = es_vertex;
            collision->ts_surface_normal_at_collision_point = ts_surface_normal;
        }
    }
}

internal_function void check_collision_with_edge(const vector3_t &es_sphere_velocity, const vector3_t &es_sphere_position, const vector3_t &es_vertex_a, const vector3_t &es_vertex_b, const vector3_t &ts_surface_normal, sphere_triangle_collision_return_t *collision)
{
    vector3_t es_edge_diff = es_vertex_b - es_vertex_a;
    vector3_t es_sphere_pos_to_vertex = es_vertex_a - es_sphere_position;
        
    float32_t a = distance_squared(es_edge_diff) * -distance_squared(es_sphere_velocity) + squared(glm::dot(es_edge_diff, es_sphere_velocity));
    float32_t b = distance_squared(es_edge_diff) * 2.0f * glm::dot(es_sphere_velocity, es_sphere_pos_to_vertex) - 2.0f * (glm::dot(es_edge_diff, es_sphere_velocity) * glm::dot(es_edge_diff, es_sphere_pos_to_vertex));
    float32_t c = distance_squared(es_edge_diff) * (1.0f - distance_squared(es_sphere_pos_to_vertex)) + squared(glm::dot(es_edge_diff, es_sphere_pos_to_vertex));

    float32_t new_resting_instance;
    if (get_smallest_root(a, b, c, 1.0f, &new_resting_instance))
    {
        float32_t in_edge_proportion = (glm::dot(es_edge_diff, es_sphere_velocity) * new_resting_instance - glm::dot(es_edge_diff, es_sphere_pos_to_vertex)) / distance_squared(es_edge_diff);
            
        if (in_edge_proportion >= 0.0f && in_edge_proportion <= 1.0f)
        {
            vector3_t es_sphere_contact_point = es_vertex_a + in_edge_proportion * es_edge_diff;
            float32_t es_distance = glm::length(new_resting_instance * es_sphere_velocity);
            
            if (es_distance < collision->es_distance)
            {
                collision->is_edge = 1;
                collision->collision_happened = 1;
                collision->es_distance = es_distance;
                collision->es_sphere_contact_point = es_sphere_contact_point;
                collision->ts_surface_normal_at_collision_point = ts_surface_normal;
            }
        }
    }
}

// TODO: Render the edge that is being detected as colliding
internal_function void check_sphere_triangle_collision(sphere_triangle_collision_return_t *collision,
                                                       terrain_triangle_t *triangle,
                                                       const vector3_t &ts_sphere_position,
                                                       const vector3_t &ts_sphere_velocity,
                                                       float32_t dt,
                                                       const vector3_t &ts_sphere_radius,
                                                       bool slide)
{
    bool found_collision = 0;
    float32_t first_resting_instance;


    
    ivector2_t ia = get_ts_xz_coord_from_idx(triangle->idx[0], terrain);
    ivector2_t ib = get_ts_xz_coord_from_idx(triangle->idx[1], terrain);
    ivector2_t ic = get_ts_xz_coord_from_idx(triangle->idx[2], terrain);

    

    // es_ = ellipsoid space
    vector3_t es_sphere_position = ts_sphere_position / ts_sphere_radius;
    vector3_t es_sphere_velocity = ts_sphere_velocity / ts_sphere_radius;


    
    vector3_t es_fa = vector3_t(ia.x, terrain->heights[triangle->idx[0]], ia.y) / ts_sphere_radius;
    vector3_t es_fb = vector3_t(ib.x, terrain->heights[triangle->idx[1]], ib.y) / ts_sphere_radius;
    vector3_t es_fc = vector3_t(ic.x, terrain->heights[triangle->idx[2]], ic.y) / ts_sphere_radius;

    

    vector3_t es_up_normal_of_triangle = glm::normalize(glm::cross(es_fb - es_fa, es_fc - es_fa));
    if (!slide)
    {
        const float32_t es_very_close_distance_from_terrain = 0.02f;
        //es_sphere_position -= es_up_normal_of_triangle * es_very_close_distance_from_terrain;
        es_sphere_velocity = -es_up_normal_of_triangle * es_very_close_distance_from_terrain;
    }
    
    vector3_t ts_surface_normal = es_up_normal_of_triangle * ts_sphere_radius;
    
    vector3_t es_normalized_ts_sphere_velocity = glm::normalize(es_sphere_velocity);
    float32_t velocity_dot_normal = glm::dot(es_normalized_ts_sphere_velocity, es_up_normal_of_triangle);
    
    if (velocity_dot_normal > 0.0f)
    {
        return;
    }

    float32_t plane_constant = -( (es_fa.x * es_up_normal_of_triangle.x) + (es_fa.y * es_up_normal_of_triangle.y) + (es_fa.z * es_up_normal_of_triangle.z) );

    bool32_t must_check_only_for_edges_and_vertices = 0;
    float32_t normal_dot_velocity = glm::dot(es_sphere_velocity, es_up_normal_of_triangle);
    float32_t sphere_plane_distance = glm::dot(es_sphere_position, es_up_normal_of_triangle) + plane_constant;
    
    if (normal_dot_velocity == 0.0f)
    {
        if (glm::abs(sphere_plane_distance) >= 1.0f)
        {
            return;
        }
        else
        {
            must_check_only_for_edges_and_vertices = 1;
        }
    }

    const float32_t very_close_distance = 0.01f;
    
    if (!must_check_only_for_edges_and_vertices)
    {
        // Check collision with triangle face
        first_resting_instance = (1.0f - sphere_plane_distance) / normal_dot_velocity;
        float32_t second_resting_instance = (-1.0f - sphere_plane_distance) / normal_dot_velocity;

        if (first_resting_instance > second_resting_instance)
        {
            float32_t f = first_resting_instance;
            first_resting_instance = second_resting_instance;
            second_resting_instance = f;
        }
        if (first_resting_instance > 1.0f || second_resting_instance < 0.0f)
        {
            return;
        }
        if (first_resting_instance < 0.0f) first_resting_instance = 0.0f;
        if (second_resting_instance < 1.0f) second_resting_instance = 1.0f;

        vector3_t es_contact_point = es_sphere_position + (first_resting_instance * es_sphere_velocity) - es_up_normal_of_triangle;

        if (is_point_in_triangle(es_contact_point, es_fa, es_fb, es_fc))
        {
            float32_t es_distance = glm::length(es_sphere_velocity * first_resting_instance);
            if (es_distance < collision->es_distance)
            {
                float32_t sphere_point_plane_distance = glm::dot(es_sphere_position - es_up_normal_of_triangle, es_up_normal_of_triangle) + plane_constant;
                if (sphere_point_plane_distance < 0.0f && !collision->sphere_was_under_the_terrain)
                {
                    // Adjust the sphere position, and call the function
                    vector3_t es_new_sphere_position = es_sphere_position - es_up_normal_of_triangle * sphere_point_plane_distance;
                    es_new_sphere_position += very_close_distance * es_up_normal_of_triangle;
                    
                    collision->sphere_was_under_the_terrain = 1;
                    collision->ts_new_sphere_position = es_new_sphere_position * ts_sphere_radius;
                    collision->ts_surface_normal_at_collision_point = ts_surface_normal;

                    check_sphere_triangle_collision(collision,
                                                    triangle,
                                                    es_new_sphere_position * ts_sphere_radius,
                                                    ts_sphere_velocity,
                                                    dt,
                                                    ts_sphere_radius,
                                                    terrain,
                                                    slide);
                    
                    return;
                }
                
                found_collision = 1;
                collision->collision_happened = 1;
                collision->es_distance = es_distance;
                collision->es_sphere_contact_point = es_contact_point;
                collision->ts_surface_normal_at_collision_point = ts_surface_normal;
            }
        }
    }
    
    if (!found_collision)
    {
        // Check collision with vertices
        check_collision_with_vertex(es_sphere_velocity, es_sphere_position, es_fa, ts_surface_normal, collision);
        check_collision_with_vertex(es_sphere_velocity, es_sphere_position, es_fb, ts_surface_normal, collision);
        check_collision_with_vertex(es_sphere_velocity, es_sphere_position, es_fc, ts_surface_normal,collision);

        // Check collision with edges
        check_collision_with_edge(es_sphere_velocity, es_sphere_position, es_fa, es_fb, ts_surface_normal, collision);
        check_collision_with_edge(es_sphere_velocity, es_sphere_position, es_fb, es_fc, ts_surface_normal, collision);
        check_collision_with_edge(es_sphere_velocity, es_sphere_position, es_fc, es_fa, ts_surface_normal, collision);
    }
}

internal_function void push_collision(const sphere_triangle_collision_return_t &collision, sphere_triangle_collision_return_t *stack, uint32_t &counter, uint32_t max)
{
    if (counter < max)
    {
        stack[counter++] = collision;
    }
}

struct collide_and_slide_collision_t
{
    bool collided;
    
    vector3_t ts_position;
    vector3_t ts_velocity;
    
    vector3_t ts_normal;

    bool is_edge;
};

internal_function void collide_with_triangle(const vector3_t &ts_sphere_position,
                                             const vector3_t &ts_sphere_velocity,
                                             const vector3_t &ts_sphere_size,
                                             float32_t dt,
                                             terrain_triangle_t *triangle,
                                             uint32_t x, uint32_t z,
                                             uint32_t x_offset0, uint32_t z_offset0,
                                             uint32_t x_offset1, uint32_t z_offset1,
                                             uint32_t x_offset2, uint32_t z_offset2,
                                             sphere_triangle_collision_return_t *closest,
                                             bool slide)
{
    triangle->triangle_exists = 1;
    triangle->idx[0] = get_terrain_index(x + x_offset0, z + z_offset0, terrain->xz_dim.x);
    triangle->idx[1] = get_terrain_index(x + x_offset1, z + z_offset1, terrain->xz_dim.x);
    triangle->idx[2] = get_terrain_index(x + x_offset2, z + z_offset2, terrain->xz_dim.x);
    check_sphere_triangle_collision(closest, triangle, ts_sphere_position, ts_sphere_velocity, dt, ts_sphere_size, terrain, slide);
}

internal_function void adjust_if_sphere_was_under_terrain(sphere_triangle_collision_return_t *collision,
                                                     vector3_t &ts_sphere_position)
{
    if (collision->sphere_was_under_the_terrain)
    {
        collision->sphere_was_under_the_terrain = 0;
        ts_sphere_position = collision->ts_new_sphere_position;
    }
}

// Get all the triangles that the sphere might collide with
internal_function collide_and_slide_collision_t detect_collision_against_possible_colliding_triangles(vector3_t ts_sphere_position,
                                                                                                      const vector3_t &ts_sphere_size,
                                                                                                      vector3_t ts_sphere_velocity,
                                                                                                      float32_t dt,
                                                                                                      bool previous_was_edge,
                                                                                                      bool slide,
                                                                                                      uint32_t recurse_depth = 0)
{
    if (terrain)
    {
        if (!slide) ts_sphere_velocity = vector3_t(0.0f);

        vector3_t ts_ceil_size = glm::ceil(ts_sphere_size);

        float32_t x_max = ts_sphere_position.x + ts_ceil_size.x;
        float32_t x_min = ts_sphere_position.x - ts_ceil_size.x;
        float32_t z_max = ts_sphere_position.z + ts_ceil_size.z;
        float32_t z_min = ts_sphere_position.z - ts_ceil_size.z;

        // Index of the vertices (not faces)
        int32_t max_x_idx = (int32_t)(glm::ceil(x_max));
        if (max_x_idx >= terrain->xz_dim.x) max_x_idx = terrain->xz_dim.x - 1;
        if (max_x_idx < 0) max_x_idx = 0;
        int32_t min_x_idx = (int32_t)(glm::floor(x_min));
        if (min_x_idx >= terrain->xz_dim.x) min_x_idx = terrain->xz_dim.x - 1;
        if (min_x_idx < 0) min_x_idx = 0;
        int32_t max_z_idx = (int32_t)(glm::ceil(z_max));
        if (max_z_idx >= terrain->xz_dim.y) max_z_idx = terrain->xz_dim.y - 1;
        if (max_z_idx < 0) max_z_idx = 0;
        int32_t min_z_idx = (int32_t)(glm::floor(z_min));
        if (min_z_idx >= terrain->xz_dim.y) min_z_idx = terrain->xz_dim.y - 1;
        if (min_z_idx < 0) min_z_idx = 0;

        int32_t x_diff = max_x_idx - min_x_idx;
        int32_t z_diff = max_z_idx - min_z_idx;

        uint32_t maximum_collisions = 5;

        sphere_triangle_collision_return_t closest_collision = {};
        closest_collision.es_distance = 1000.0f;
        
        //uint32_t collision_count = 0;
        //sphere_triangle_collision_return_t *collisions = (sphere_triangle_collision_return_t *)allocate_linear(sizeof(sphere_triangle_collision_return_t) * maximum_collisions);
        
        memory_buffer_view_t<terrain_triangle_t> triangles;
        triangles.count = x_diff * z_diff * 2;
        triangles.buffer = ALLOCA_T(terrain_triangle_t, x_diff * z_diff * 2);
        // TODO: Fix linear allocator
        // triangles.buffer = (terrain_triangle_t *)allocate_linear(sizeof(terrain_triangle_t) * x_diff * z_diff * 2);

        uint32_t triangle_counter = 0;
        for (int32_t x = min_x_idx; x < max_x_idx; ++x)
        {
            for (int32_t z = min_z_idx; z < max_z_idx; ++z)
            {
                if (x % 2 == 0)
                {
                    if (z % 2 == 0)
                    {
                        collide_with_triangle(ts_sphere_position, ts_sphere_velocity, ts_sphere_size, dt, &triangles[triangle_counter++], x, z, 0, 0, 0, 1, 1, 1, terrain, &closest_collision, slide);
                        adjust_if_sphere_was_under_terrain(&closest_collision, ts_sphere_position);
                        collide_with_triangle(ts_sphere_position, ts_sphere_velocity, ts_sphere_size, dt, &triangles[triangle_counter++], x, z, 0, 0, 1, 1, 1, 0, terrain, &closest_collision, slide);
                        adjust_if_sphere_was_under_terrain(&closest_collision, ts_sphere_position);
                    }
                    else
                    {
                        collide_with_triangle(ts_sphere_position, ts_sphere_velocity, ts_sphere_size, dt, &triangles[triangle_counter++], x, z, 0, 1, 1, 0, 0, 0, terrain, &closest_collision, slide);
                        adjust_if_sphere_was_under_terrain(&closest_collision, ts_sphere_position);
                        collide_with_triangle(ts_sphere_position, ts_sphere_velocity, ts_sphere_size, dt, &triangles[triangle_counter++], x, z, 0, 1, 1, 1, 1, 0, terrain, &closest_collision, slide);
                        adjust_if_sphere_was_under_terrain(&closest_collision, ts_sphere_position);
                    }
                }
                else
                {
                    if (z % 2 == 0)
                    {
                        collide_with_triangle(ts_sphere_position, ts_sphere_velocity, ts_sphere_size, dt, &triangles[triangle_counter++], x, z, 0, 1, 1, 0, 0, 0, terrain, &closest_collision, slide);
                        adjust_if_sphere_was_under_terrain(&closest_collision, ts_sphere_position);
                        collide_with_triangle(ts_sphere_position, ts_sphere_velocity, ts_sphere_size, dt, &triangles[triangle_counter++], x, z, 0, 1, 1, 1, 1, 0, terrain, &closest_collision, slide);
                        adjust_if_sphere_was_under_terrain(&closest_collision, ts_sphere_position);
                    }
                    else
                    {
                        collide_with_triangle(ts_sphere_position, ts_sphere_velocity, ts_sphere_size, dt, &triangles[triangle_counter++], x, z, 0, 0, 0, 1, 1, 1, terrain, &closest_collision, slide);
                        adjust_if_sphere_was_under_terrain(&closest_collision, ts_sphere_position);
                        collide_with_triangle(ts_sphere_position, ts_sphere_velocity, ts_sphere_size, dt, &triangles[triangle_counter++], x, z, 0, 0, 1, 1, 1, 0, terrain, &closest_collision, slide);
                        adjust_if_sphere_was_under_terrain(&closest_collision, ts_sphere_position);
                    }
                }
            }
        }

        const float32_t es_very_close_distance_from_terrain = 0.01f;
        
        if (closest_collision.collision_happened)
        {
            if (!slide)
            {
                collide_and_slide_collision_t collision = {};
                collision.is_edge = 0;
                collision.collided = closest_collision.collision_happened;
                collision.ts_normal = closest_collision.ts_surface_normal_at_collision_point;
                return collision;
            }
            
            uint32_t max_recursion_depth = 5;

            // TODO: Do not calculate the ellipsoid space of these values again, just do it once at the beginning of the function
            vector3_t es_sphere_position = ts_sphere_position / ts_sphere_size;
            vector3_t es_sphere_velocity = ts_sphere_velocity / ts_sphere_size;

            vector3_t es_new_sphere_position = es_sphere_position;
            vector3_t es_sphere_destination_point = es_sphere_position + es_sphere_velocity;
            
            if (closest_collision.es_distance >= es_very_close_distance_from_terrain)
            {
                vector3_t es_normalized_velocity = glm::normalize(es_sphere_velocity);
                vector3_t es_scaled_velocity = es_normalized_velocity * (closest_collision.es_distance - es_very_close_distance_from_terrain);
                es_new_sphere_position = es_sphere_position + es_scaled_velocity;

                closest_collision.es_sphere_contact_point -= es_very_close_distance_from_terrain * es_normalized_velocity;
            }

            // Get slide plane information
            vector3_t es_slide_plane_point = closest_collision.es_sphere_contact_point;
            vector3_t es_slide_plane_normal = glm::normalize(es_new_sphere_position - closest_collision.es_sphere_contact_point);

            float32_t plane_constant = get_plane_constant(es_slide_plane_point, es_slide_plane_normal);
            float32_t dest_point_dist_from_plane = glm::dot(es_sphere_destination_point, es_slide_plane_normal) + plane_constant;

            vector3_t es_new_sphere_destination_point = es_sphere_destination_point - dest_point_dist_from_plane * es_slide_plane_normal;
            vector3_t es_new_velocity = es_new_sphere_destination_point - closest_collision.es_sphere_contact_point;

            float32_t new_velocity_distance_squared = distance_squared(es_new_velocity);
            float32_t very_close_distance_squared = squared(es_very_close_distance_from_terrain);

            if (new_velocity_distance_squared < very_close_distance_squared)
            {
                collide_and_slide_collision_t ret = {};
                ret.collided = 1;
                ret.ts_position = es_new_sphere_position * ts_sphere_size;
                ret.ts_velocity = es_new_velocity * ts_sphere_size;
                ret.ts_normal = es_slide_plane_normal * ts_sphere_size;
                return(ret);
            }
            // There was a collision, must recurse
            else if (recurse_depth < max_recursion_depth && slide)
            {
                return detect_collision_against_possible_colliding_triangles(terrain,
                                                                             es_new_sphere_position * ts_sphere_size,
                                                                             ts_sphere_size,
                                                                             es_new_velocity * ts_sphere_size,
                                                                             dt,
                                                                             0,
                                                                             1,
                                                                             recurse_depth + 1);
            }
            else
            {
                collide_and_slide_collision_t ret = {};
                ret.collided = 1;
                ret.ts_position = es_new_sphere_position * ts_sphere_size;
                ret.ts_velocity = es_new_velocity * ts_sphere_size;
                ret.ts_normal = es_slide_plane_normal * ts_sphere_size;
                return(ret);
            }
        }
        else
        {
            collide_and_slide_collision_t ret = {};
            ret.collided = 0;
            ret.ts_position = ts_sphere_position + ts_sphere_velocity;
            ret.ts_velocity = ts_sphere_velocity;
            return(ret);
        }
    }
    else return {};
}

struct detected_collision_return_t
{
    bool detected;
    vector3_t ws_at;
    vector3_t ts_at;
    vector3_t ws_normal;
    vector3_t ts_normal;

    float32_t ts_y_diff;
};

enum terrain_space_t { TERRAIN_SPACE, WORLD_SPACE };

internal_function detected_collision_return_t detect_terrain_collision(hitbox_t *hitbox,
                                                                       const vector3_t &size,
                                                                       const vector3_t &ws_p,
                                                                       morphable_terrain_t *t,
                                                                       enum terrain_space_t terrain_space = terrain_space_t::WORLD_SPACE);

internal_function collide_and_slide_collision_t detect_and_stick_collision_against_possible_colliding_triangles(morphable_terrain_t *terrain,
                                                                                                                vector3_t ts_sphere_position,
                                                                                                                const vector3_t &ts_sphere_size,
                                                                                                                vector3_t ts_sphere_velocity,
                                                                                                                float32_t dt,
                                                                                                                uint32_t recurse_depth = 0)
{
    if (terrain)
    {
        //if (!slide) ts_sphere_velocity = vector3_t(0.0f);

        vector3_t ts_ceil_size = glm::ceil(ts_sphere_size);

        float32_t x_max = ts_sphere_position.x + ts_ceil_size.x;
        float32_t x_min = ts_sphere_position.x - ts_ceil_size.x;
        float32_t z_max = ts_sphere_position.z + ts_ceil_size.z;
        float32_t z_min = ts_sphere_position.z - ts_ceil_size.z;

        // Index of the vertices (not faces)
        int32_t max_x_idx = (int32_t)(glm::ceil(x_max));
        if (max_x_idx >= terrain->xz_dim.x) max_x_idx = terrain->xz_dim.x - 1;
        if (max_x_idx < 0) max_x_idx = 0;
        int32_t min_x_idx = (int32_t)(glm::floor(x_min));
        if (min_x_idx >= terrain->xz_dim.x) min_x_idx = terrain->xz_dim.x - 1;
        if (min_x_idx < 0) min_x_idx = 0;
        int32_t max_z_idx = (int32_t)(glm::ceil(z_max));
        if (max_z_idx >= terrain->xz_dim.y) max_z_idx = terrain->xz_dim.y - 1;
        if (max_z_idx < 0) max_z_idx = 0;
        int32_t min_z_idx = (int32_t)(glm::floor(z_min));
        if (min_z_idx >= terrain->xz_dim.y) min_z_idx = terrain->xz_dim.y - 1;
        if (min_z_idx < 0) min_z_idx = 0;

        int32_t x_diff = max_x_idx - min_x_idx;
        int32_t z_diff = max_z_idx - min_z_idx;

        uint32_t maximum_collisions = 5;

        sphere_triangle_collision_return_t closest_collision = {};
        closest_collision.es_distance = 1000.0f;
        
        //uint32_t collision_count = 0;
        //sphere_triangle_collision_return_t *collisions = (sphere_triangle_collision_return_t *)allocate_linear(sizeof(sphere_triangle_collision_return_t) * maximum_collisions);
        
        memory_buffer_view_t<terrain_triangle_t> triangles;
        triangles.count = x_diff * z_diff * 2;
        triangles.buffer = ALLOCA_T(terrain_triangle_t, x_diff * z_diff * 2);
        // TODO: Fix linear allocator
        // triangles.buffer = (terrain_triangle_t *)allocate_linear(sizeof(terrain_triangle_t) * x_diff * z_diff * 2);

        uint32_t triangle_counter = 0;
        for (int32_t x = min_x_idx; x < max_x_idx; ++x)
        {
            for (int32_t z = min_z_idx; z < max_z_idx; ++z)
            {
                if (x % 2 == 0)
                {
                    if (z % 2 == 0)
                    {
                        collide_with_triangle(ts_sphere_position, ts_sphere_velocity, ts_sphere_size, dt, &triangles[triangle_counter++], x, z, 0, 0, 0, 1, 1, 1, terrain, &closest_collision, 1);
                        adjust_if_sphere_was_under_terrain(&closest_collision, ts_sphere_position);
                        collide_with_triangle(ts_sphere_position, ts_sphere_velocity, ts_sphere_size, dt, &triangles[triangle_counter++], x, z, 0, 0, 1, 1, 1, 0, terrain, &closest_collision, 1);
                        adjust_if_sphere_was_under_terrain(&closest_collision, ts_sphere_position);
                    }
                    else
                    {
                        collide_with_triangle(ts_sphere_position, ts_sphere_velocity, ts_sphere_size, dt, &triangles[triangle_counter++], x, z, 0, 1, 1, 0, 0, 0, terrain, &closest_collision, 1);
                        adjust_if_sphere_was_under_terrain(&closest_collision, ts_sphere_position);
                        collide_with_triangle(ts_sphere_position, ts_sphere_velocity, ts_sphere_size, dt, &triangles[triangle_counter++], x, z, 0, 1, 1, 1, 1, 0, terrain, &closest_collision, 1);
                        adjust_if_sphere_was_under_terrain(&closest_collision, ts_sphere_position);
                    }
                }
                else
                {
                    if (z % 2 == 0)
                    {
                        collide_with_triangle(ts_sphere_position, ts_sphere_velocity, ts_sphere_size, dt, &triangles[triangle_counter++], x, z, 0, 1, 1, 0, 0, 0, terrain, &closest_collision, 1);
                        adjust_if_sphere_was_under_terrain(&closest_collision, ts_sphere_position);
                        collide_with_triangle(ts_sphere_position, ts_sphere_velocity, ts_sphere_size, dt, &triangles[triangle_counter++], x, z, 0, 1, 1, 1, 1, 0, terrain, &closest_collision, 1);
                        adjust_if_sphere_was_under_terrain(&closest_collision, ts_sphere_position);
                    }
                    else
                    {
                        collide_with_triangle(ts_sphere_position, ts_sphere_velocity, ts_sphere_size, dt, &triangles[triangle_counter++], x, z, 0, 0, 0, 1, 1, 1, terrain, &closest_collision, 1);
                        adjust_if_sphere_was_under_terrain(&closest_collision, ts_sphere_position);
                        collide_with_triangle(ts_sphere_position, ts_sphere_velocity, ts_sphere_size, dt, &triangles[triangle_counter++], x, z, 0, 0, 1, 1, 1, 0, terrain, &closest_collision, 1);
                        adjust_if_sphere_was_under_terrain(&closest_collision, ts_sphere_position);
                    }
                }
            }
        }

        const float32_t es_very_close_distance_from_terrain = 0.01f;
        
        if (closest_collision.collision_happened)
        {
            if (closest_collision.is_edge)
            {
                //                OutputDebugString("Hit edge\n");
            }
            
            if (!(true))
            {
                collide_and_slide_collision_t collision = {};
                collision.is_edge = 0;
                collision.collided = closest_collision.collision_happened;
                collision.ts_normal = closest_collision.ts_surface_normal_at_collision_point;
                return collision;
            }
            
            uint32_t max_recursion_depth = 5;

            // TODO: Do not calculate the ellipsoid space of these values again, just do it once at the beginning of the function
            vector3_t es_sphere_position = ts_sphere_position / ts_sphere_size;
            vector3_t es_sphere_velocity = ts_sphere_velocity / ts_sphere_size;

            vector3_t es_new_sphere_position = es_sphere_position;
            vector3_t es_sphere_destination_point = es_sphere_position + es_sphere_velocity;
            
            if (closest_collision.es_distance >= es_very_close_distance_from_terrain)
            {
                vector3_t es_normalized_velocity = glm::normalize(es_sphere_velocity);
                vector3_t es_scaled_velocity = es_normalized_velocity * (closest_collision.es_distance - es_very_close_distance_from_terrain);
                es_new_sphere_position = es_sphere_position + es_scaled_velocity;

                closest_collision.es_sphere_contact_point -= es_very_close_distance_from_terrain * es_normalized_velocity;
            }

            // Get slide plane information
            vector3_t es_slide_plane_point = closest_collision.es_sphere_contact_point;
            vector3_t es_slide_plane_normal = glm::normalize(es_new_sphere_position - closest_collision.es_sphere_contact_point);

            float32_t plane_constant = get_plane_constant(es_slide_plane_point, es_slide_plane_normal);
            float32_t dest_point_dist_from_plane = glm::dot(es_sphere_destination_point, es_slide_plane_normal) + plane_constant;

            vector3_t es_new_sphere_destination_point = es_sphere_destination_point - dest_point_dist_from_plane * es_slide_plane_normal;
            vector3_t es_new_velocity = es_new_sphere_destination_point - closest_collision.es_sphere_contact_point;

            float32_t new_velocity_distance_squared = distance_squared(es_new_velocity);
            float32_t very_close_distance_squared = squared(es_very_close_distance_from_terrain);

            if (new_velocity_distance_squared < very_close_distance_squared)
            {
                collide_and_slide_collision_t ret = {};
                ret.collided = 1;
                ret.ts_position = es_new_sphere_position * ts_sphere_size;
                ret.ts_velocity = es_new_velocity * ts_sphere_size;
                ret.ts_normal = es_slide_plane_normal * ts_sphere_size;
                return(ret);
            }
            // There was a collision, must recurse
            else if (recurse_depth < max_recursion_depth && true)
            {
                if (closest_collision.is_edge)
                {
                    collide_and_slide_collision_t collision = detect_and_stick_collision_against_possible_colliding_triangles(terrain,
                                                                                                                              es_new_sphere_position * ts_sphere_size,
                                                                                                                              ts_sphere_size,
                                                                                                                              es_new_velocity * ts_sphere_size,
                                                                                                                              dt,
                                                                                                                              recurse_depth + 1);

                    if (!collision.collided)
                    {
                        // Get which 
                        detected_collision_return_t ret = detect_terrain_collision(nullptr,
                                                                                   ts_sphere_size,
                                                                                   ts_sphere_position,
                                                                                   terrain,
                                                                                   terrain_space_t::TERRAIN_SPACE);

                        // Don't need to divide by size but whatever
                        vector3_t es_new_sliding_direction = glm::normalize(get_sliding_down_direction_ts(ts_sphere_velocity, vector3_t(0.0f, 1.0f, 0.0f), ret.ts_normal) / ts_sphere_size);
                        
                        float32_t scale_of_new_sliding_direction = glm::length(es_new_velocity);
                        es_new_sliding_direction *= scale_of_new_sliding_direction;
                        es_new_sliding_direction -= vector3_t(0.0f, es_very_close_distance_from_terrain, 0.0f);

                        collision = detect_and_stick_collision_against_possible_colliding_triangles(terrain,
                                                                                                    es_new_sphere_position * ts_sphere_size,
                                                                                                    ts_sphere_size,
                                                                                                    es_new_sliding_direction * ts_sphere_size,
                                                                                                    dt,
                                                                                                    recurse_depth + 1);
                    }

                    return collision;
                }
                else
                {
                    return detect_and_stick_collision_against_possible_colliding_triangles(terrain,
                                                                                           es_new_sphere_position * ts_sphere_size,
                                                                                           ts_sphere_size,
                                                                                           es_new_velocity * ts_sphere_size,
                                                                                           dt,
                                                                                           recurse_depth + 1);
                }
            }
            else
            {
                collide_and_slide_collision_t ret = {};
                ret.collided = 1;
                ret.ts_position = es_new_sphere_position * ts_sphere_size;
                ret.ts_velocity = es_new_velocity * ts_sphere_size;
                ret.ts_normal = es_slide_plane_normal * ts_sphere_size;
                return(ret);
            }
        }
        else
        {
            collide_and_slide_collision_t ret = {};
            ret.collided = 0;
            ret.ts_position = ts_sphere_position + ts_sphere_velocity;
            ret.ts_velocity = ts_sphere_velocity;
            return(ret);
        }
    }
    else return {};
}

void adjust_closest_distance_with_triangle(const vector3_t &ts_sphere_position,
                                           const vector3_t &ts_sphere_radius,
                                           uint32_t x, uint32_t z,
                                           uint32_t x_offset0, uint32_t z_offset0,
                                           uint32_t x_offset1, uint32_t z_offset1,
                                           uint32_t x_offset2, uint32_t z_offset2,
                                           morphable_terrain_t *terrain,
                                           float32_t *closest_distance)
{
    terrain_triangle_t triangle = {};
    triangle.idx[0] = get_terrain_index(x + x_offset0, z + z_offset0, terrain->xz_dim.x);
    triangle.idx[1] = get_terrain_index(x + x_offset1, z + z_offset1, terrain->xz_dim.x);
    triangle.idx[2] = get_terrain_index(x + x_offset2, z + z_offset2, terrain->xz_dim.x);

    float32_t first_resting_instance;
    
    ivector2_t ia = get_ts_xz_coord_from_idx(triangle.idx[0], terrain);
    ivector2_t ib = get_ts_xz_coord_from_idx(triangle.idx[1], terrain);
    ivector2_t ic = get_ts_xz_coord_from_idx(triangle.idx[2], terrain);
   // es_ = ellipsoid space
    vector3_t es_sphere_position = ts_sphere_position / ts_sphere_radius;
    
    vector3_t es_fa = vector3_t(ia.x, terrain->heights[triangle.idx[0]], ia.y) / ts_sphere_radius;
    vector3_t es_fb = vector3_t(ib.x, terrain->heights[triangle.idx[1]], ib.y) / ts_sphere_radius;
    vector3_t es_fc = vector3_t(ic.x, terrain->heights[triangle.idx[2]], ic.y) / ts_sphere_radius;

    vector3_t es_up_normal_of_triangle = glm::normalize(glm::cross(es_fb - es_fa, es_fc - es_fa));
    vector3_t es_sphere_velocity = -es_up_normal_of_triangle;
    
    vector3_t ts_surface_normal = es_up_normal_of_triangle * ts_sphere_radius;
    
    vector3_t es_normalized_ts_sphere_velocity = glm::normalize(es_sphere_velocity);
    float32_t velocity_dot_normal = glm::dot(es_normalized_ts_sphere_velocity, es_up_normal_of_triangle);
    
    if (velocity_dot_normal > 0.0f)
    {
        return;
    }

    float32_t plane_constant = -( (es_fa.x * es_up_normal_of_triangle.x) + (es_fa.y * es_up_normal_of_triangle.y) + (es_fa.z * es_up_normal_of_triangle.z) );

    bool32_t must_check_only_for_edges_and_vertices = 0;
    float32_t normal_dot_velocity = glm::dot(es_sphere_velocity, es_up_normal_of_triangle);
    float32_t sphere_plane_distance = glm::dot(es_sphere_position, es_up_normal_of_triangle) + plane_constant;
    
    if (normal_dot_velocity == 0.0f)
    {
        if (glm::abs(sphere_plane_distance) >= 1.0f)
        {
            return;
        }
        else
        {
            must_check_only_for_edges_and_vertices = 1;
        }
    }

    const float32_t very_close_distance = 0.01f;
    
    if (!must_check_only_for_edges_and_vertices)
    {
        // Check collision with triangle face
        first_resting_instance = (1.0f - sphere_plane_distance) / normal_dot_velocity;
        float32_t second_resting_instance = (-1.0f - sphere_plane_distance) / normal_dot_velocity;

        if (first_resting_instance > second_resting_instance)
        {
            float32_t f = first_resting_instance;
            first_resting_instance = second_resting_instance;
            second_resting_instance = f;
        }
        if (second_resting_instance < 0.0f)
        {
            return;
        }
        if (first_resting_instance < 0.0f) first_resting_instance = 0.0f;

        vector3_t es_contact_point = es_sphere_position + (first_resting_instance * es_sphere_velocity) - es_up_normal_of_triangle;

        if (is_point_in_triangle(es_contact_point, es_fa, es_fb, es_fc))
        {
            float32_t es_distance = glm::length(es_sphere_velocity * first_resting_instance);
            if (es_distance < *closest_distance)
            {
                *closest_distance = es_distance;
                
                //found_collision = 1;
                //collision->collision_happened = 1;
                //collision->es_distance = es_distance;
                //collision->es_sphere_contact_point = es_contact_point;
                //collision->ts_surface_normal_at_collision_point = ts_surface_normal;
            }
        }
    }
}

void reinitialize_terrain_graphics_data(void)
{
    /*auto *terrain_ppln = g_pipeline_manager->get(g_terrains->terrain_ppln);
    auto *terrain_shadow_ppln = g_pipeline_manager->get(g_terrains->terrain_shadow_ppln);
    
    destroy_pipeline(&terrain_ppln->pipeline);
    destroy_pipeline(&terrain_shadow_ppln->pipeline);
    
    terrain_base_info_t *base = get_terrain_base(0);
    auto *model_info = &base->model_info;    

    {
        render_pass_handle_t dfr_render_pass = g_render_pass_manager->get_handle("render_pass.deferred_render_pass"_hash);
        shader_modules_t modules(shader_module_info_t{"shaders/SPV/terrain.vert.spv", VK_SHADER_STAGE_VERTEX_BIT},
                               shader_module_info_t{"shaders/SPV/terrain.geom.spv", VK_SHADER_STAGE_GEOMETRY_BIT},
                               shader_module_info_t{"shaders/SPV/terrain.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT});
        shader_uniform_layouts_t layouts(g_uniform_layout_manager->get_handle("uniform_layout.camera_transforms_ubo"_hash),
                                       g_uniform_layout_manager->get_handle("descriptor_set_layout.2D_sampler_layout"_hash));
        shader_pk_data_t push_k = {160, 0, VK_SHADER_STAGE_VERTEX_BIT};
        shader_blend_states_t blending(false, false, false, false);
        dynamic_states_t dynamic(VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_LINE_WIDTH);
        fill_graphics_pipeline_info(modules, VK_TRUE, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN, VK_POLYGON_MODE_FILL,
                                    VK_CULL_MODE_NONE, layouts, push_k, get_backbuffer_resolution(), blending, model_info,
                                    true, 0.0f, dynamic, g_render_pass_manager->get(dfr_render_pass), 0, terrain_ppln->info);
        make_graphics_pipeline(terrain_ppln);
    }

    {
        auto shadow_display = get_shadow_display();
        VkExtent2D shadow_extent = VkExtent2D{shadow_display.shadowmap_w, shadow_display.shadowmap_h};
        render_pass_handle_t shadow_render_pass = g_render_pass_manager->get_handle("render_pass.shadow_render_pass"_hash);
        shader_modules_t modules(shader_module_info_t{"shaders/SPV/terrain_shadow.vert.spv", VK_SHADER_STAGE_VERTEX_BIT},
                               shader_module_info_t{"shaders/SPV/terrain_shadow.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT});
        shader_uniform_layouts_t layouts(g_uniform_layout_manager->get_handle("uniform_layout.camera_transforms_ubo"_hash));
        shader_pk_data_t push_k = {160, 0, VK_SHADER_STAGE_VERTEX_BIT};
        shader_blend_states_t blending = {};
        dynamic_states_t dynamic(VK_DYNAMIC_STATE_DEPTH_BIAS, VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_LINE_WIDTH);
        fill_graphics_pipeline_info(modules, VK_TRUE, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN, VK_POLYGON_MODE_FILL,
                                    VK_CULL_MODE_NONE, layouts, push_k, shadow_extent, blending, model_info,
                                    true, 0.0f, dynamic, g_render_pass_manager->get(shadow_render_pass), 0, terrain_shadow_ppln->info);
        make_graphics_pipeline(terrain_shadow_ppln);
        }*/
}

internal_function void initialize_graphics_terrain_data(VkCommandPool *cmdpool)
{
    /*terrain_base_info_t *base = get_terrain_base(0);
    auto *model_info = &base->model_info;

    g_terrains->terrain_ppln = g_pipeline_manager->add("pipeline.terrain_pipeline"_hash);
    auto *terrain_ppln = g_pipeline_manager->get(g_terrains->terrain_ppln);
    {
        graphics_pipeline_info_t *info = (graphics_pipeline_info_t *)allocate_free_list(sizeof(graphics_pipeline_info_t));
        render_pass_handle_t dfr_render_pass = g_render_pass_manager->get_handle("render_pass.deferred_render_pass"_hash);
        shader_modules_t modules(shader_module_info_t{"shaders/SPV/terrain.vert.spv", VK_SHADER_STAGE_VERTEX_BIT},
                               shader_module_info_t{"shaders/SPV/terrain.geom.spv", VK_SHADER_STAGE_GEOMETRY_BIT},
                               shader_module_info_t{"shaders/SPV/terrain.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT});
        shader_uniform_layouts_t layouts(g_uniform_layout_manager->get_handle("uniform_layout.camera_transforms_ubo"_hash),
                                       g_uniform_layout_manager->get_handle("descriptor_set_layout.2D_sampler_layout"_hash));
        shader_pk_data_t push_k = {160, 0, VK_SHADER_STAGE_VERTEX_BIT};
        shader_blend_states_t blending(false, false, false, false);
        dynamic_states_t dynamic(VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_LINE_WIDTH);
        fill_graphics_pipeline_info(modules, VK_TRUE, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN, VK_POLYGON_MODE_FILL,
                                    VK_CULL_MODE_NONE, layouts, push_k, get_backbuffer_resolution(), blending, model_info,
                                    true, 0.0f, dynamic, g_render_pass_manager->get(dfr_render_pass), 0, info);
        terrain_ppln->info = info;
        make_graphics_pipeline(terrain_ppln);
    }

    g_terrains->terrain_shadow_ppln = g_pipeline_manager->add("pipeline.terrain_shadow"_hash);
    auto *terrain_shadow_ppln = g_pipeline_manager->get(g_terrains->terrain_shadow_ppln);
    {
        graphics_pipeline_info_t *info = (graphics_pipeline_info_t *)allocate_free_list(sizeof(graphics_pipeline_info_t));
        auto shadow_display = get_shadow_display();
        VkExtent2D shadow_extent = VkExtent2D{shadow_display.shadowmap_w, shadow_display.shadowmap_h};
        render_pass_handle_t shadow_render_pass = g_render_pass_manager->get_handle("render_pass.shadow_render_pass"_hash);
        shader_modules_t modules(shader_module_info_t{"shaders/SPV/terrain_shadow.vert.spv", VK_SHADER_STAGE_VERTEX_BIT},
                               shader_module_info_t{"shaders/SPV/terrain_shadow.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT});
        shader_uniform_layouts_t layouts(g_uniform_layout_manager->get_handle("uniform_layout.camera_transforms_ubo"_hash));
        shader_pk_data_t push_k = {160, 0, VK_SHADER_STAGE_VERTEX_BIT};
        shader_blend_states_t blending = {};
        dynamic_states_t dynamic(VK_DYNAMIC_STATE_DEPTH_BIAS, VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_LINE_WIDTH);
        fill_graphics_pipeline_info(modules, VK_TRUE, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN, VK_POLYGON_MODE_FILL,
                                    VK_CULL_MODE_NONE, layouts, push_k, shadow_extent, blending, model_info,
                                    true, 0.0f, dynamic, g_render_pass_manager->get(shadow_render_pass), 0, info);
        terrain_shadow_ppln->info = info;
        make_graphics_pipeline(terrain_shadow_ppln);
    }
    
    make_terrain_pointer();*/
}

internal_function void prepare_terrain_pointer_for_render(gpu_command_queue_t *queue, VkDescriptorSet *ubo_set)
{
    // if the get_coord_pointing_at returns a coord with a negative - player is not pointing at the terrain
    //    if (g_terrains->terrain_pointer.ts_position.x >= 0)
    /*    {
	auto *ppln = g_pipeline_manager->get(g_terrains->terrain_pointer.ppln);
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

	push_k.ts_to_ws_terrain_model = g_terrains->terrain_pointer.t->push_k.transform;
	push_k.color = vector4_t(1.0f);
	push_k.ts_center_position = vector4_t((float32_t)g_terrains->terrain_pointer.ts_position.x
					      , 0.0f
					      , (float32_t)g_terrains->terrain_pointer.ts_position.y
					      , 1.0f);

	uint32_t x = g_terrains->terrain_pointer.ts_position.x;
	uint32_t z = g_terrains->terrain_pointer.ts_position.y;
	uint32_t width = g_terrains->terrain_pointer.t->xz_dim.x;
	uint32_t depth = g_terrains->terrain_pointer.t->xz_dim.y;
	float32_t *heights = (float32_t *)(g_terrains->terrain_pointer.t->heights);

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

// TODO: Possibly remove the component system and just stick all the entity data into one big block of memory
internal_function entity_t *get_main_entity(void)
{
    if (g_entities->main_entity == -1)
    {
        return nullptr;
    }
    else
    {
        return &g_entities->entity_list[g_entities->main_entity];
    }
}

internal_function void push_entity_to_queue(entity_t *e_ptr, mesh_t *mesh, gpu_material_submission_queue_t *queue)
{
    rendering_component_t *component = &g_entities->rendering_components[ e_ptr->components.rendering_component ];

    uniform_group_t *group = nullptr;
    
    if (e_ptr->components.animation_component >= 0)
    {
        struct animation_component_t *component = &g_entities->animation_components[ e_ptr->components.animation_component ];
        group = &component->animation_instance.group;
    }
    
    queue->push_material(&component->push_k,
			 sizeof(component->push_k),
                         mesh,
                         group);
}

internal_function void push_entity_to_animated_queue(entity_t *e)
{
    push_entity_to_queue(e, &g_entities->entity_mesh, &g_world_submission_queues[ENTITY_QUEUE]);
}

internal_function void push_entity_to_rolling_queue(entity_t *e)
{
    rendering_component_t *component = &g_entities->rendering_components[ e->components.rendering_component ];

    uniform_group_t *group = nullptr;
    
    g_world_submission_queues[ROLLING_ENTITY_QUEUE].push_material(&component->push_k,
                                                                  sizeof(component->push_k),
                                                                  &g_entities->rolling_entity_mesh,
                                                                  group);
}

entity_t construct_entity(const constant_string_t &name, vector3_t gs_p, vector3_t ws_d, quaternion_t gs_r)
{
    entity_t e;
    //    e.is_group = is_group;
    e.ws_p = gs_p;
    e.ws_d = ws_d;
    e.ws_r = gs_r;
    e.id = name;
    return(e);
}

internal_function entity_t *get_entity(const constant_string_t &name)
{
    entity_handle_t v = *g_entities->name_map.get(name.hash);
    return(&g_entities->entity_list[v]);
}

entity_t *get_entity(entity_handle_t v)
{
    return(&g_entities->entity_list[v]);
}

void attach_camera_to_entity(entity_t *e, int32_t camera_index)
{
    
}

internal_function struct camera_component_t * add_camera_component(entity_t *e, uint32_t camera_index)
{
    e->components.camera_component = g_entities->camera_component_count++;
    camera_component_t *component = &g_entities->camera_components[ e->components.camera_component ];
    component->entity_index = e->index;
    component->camera = camera_index;

    return(component);
}

internal_function void update_camera_components(float32_t dt)
{
    for (uint32_t i = 0; i < g_entities->camera_component_count; ++i)
    {
        struct camera_component_t *component = &g_entities->camera_components[ i ];
        struct camera_t *camera = get_camera(component->camera);
        entity_t *e = &g_entities->entity_list[ component->entity_index ];

        vector3_t up = vector3_t(0.0f, 1.0f, 0.0f);
        
        vector3_t camera_position = e->ws_p + vector3_t(0.0f, 1.0f, 0.0f);
        if (component->is_third_person)
        {
            //            matrix4_t lateral_rotation_offset = glm::rotate(glm::radians(10.0f), e->on_t->ws_n);
            vector3_t right = glm::cross(e->ws_d, vector3_t(0.0f, 1.0f, 0.0f));
            camera_position += right * 5.0f + -component->distance_from_player * e->ws_d;
        }
        
        camera->v_m = glm::lookAt(camera_position, e->ws_p + vector3_t(0.0f, 1.0f, 0.0f) + e->ws_d, up);

        // TODO: Don't need to calculate this every frame, just when parameters change
        camera->compute_projection();

        camera->p = camera_position;
        camera->d = e->ws_d;
        camera->u = up;
    }
}

internal_function struct rendering_component_t *add_rendering_component(entity_t *e)
{
    e->components.rendering_component = g_entities->rendering_component_count++;
    rendering_component_t *component = &g_entities->rendering_components[ e->components.rendering_component ];
    component->entity_index = e->index;
    component->push_k = {};

    return(component);
}

internal_function struct animation_component_t *add_animation_component(entity_t *e,
                                                                        uniform_layout_t *ubo_layout,
                                                                        skeleton_t *skeleton,
                                                                        animation_cycles_t *cycles,
                                                                        gpu_command_queue_pool_t *cmdpool)
{
    e->components.animation_component = g_entities->animation_component_count++;
    animation_component_t *component = &g_entities->animation_components[ e->components.animation_component ];
    component->entity_index = e->index;
    component->cycles = cycles;
    component->animation_instance = initialize_animated_instance(cmdpool,
                                                                 ubo_layout,
                                                                 skeleton,
                                                                 cycles);
    switch_to_cycle(&component->animation_instance, entity_t::animated_state_t::IDLE, 1);

    return(component);
}

internal_function void update_animation_component(float32_t dt)
{
    for (uint32_t i = 0; i < g_entities->animation_component_count; ++i)
    {
        struct animation_component_t *component = &g_entities->animation_components[ i ];
        entity_t *e = &g_entities->entity_list[ component->entity_index ];

        entity_t::animated_state_t previous_state = e->animated_state;
        entity_t::animated_state_t new_state;
        
        uint32_t moving = 0;
        
        if (e->action_flags & (1 << action_flags_t::ACTION_FORWARD))
        {
            if (e->action_flags & (1 << action_flags_t::ACTION_RUN))
            {
                new_state = entity_t::animated_state_t::RUN; moving = 1;
            }
            else
            {
                new_state = entity_t::animated_state_t::WALK; moving = 1;
            }
        }
        if (e->action_flags & (1 << action_flags_t::ACTION_LEFT)); 
        if (e->action_flags & (1 << action_flags_t::ACTION_DOWN));
        if (e->action_flags & (1 << action_flags_t::ACTION_RIGHT));
        
        if (!moving)
        {
            new_state = entity_t::animated_state_t::IDLE;
        }

        if (e->is_sitting)
        {
            new_state = entity_t::animated_state_t::SITTING;
        }

        if (e->is_in_air)
        {
            new_state = entity_t::animated_state_t::HOVER;
        }

        if (e->is_sliding_not_rolling_mode)
        {
            new_state = entity_t::animated_state_t::SLIDING_NOT_ROLLING_MODE;
        }

        if (new_state != previous_state)
        {
            e->animated_state = new_state;
            switch_to_cycle(&component->animation_instance, new_state);
        }
        
        interpolate_skeleton_joints_into_instance(dt, &component->animation_instance);
    }
}

internal_function void update_animation_gpu_data(gpu_command_queue_t *queue)
{
    for (uint32_t i = 0; i < g_entities->animation_component_count; ++i)
    {
        struct animation_component_t *component = &g_entities->animation_components[ i ];
        entity_t *e = &g_entities->entity_list[ component->entity_index ];

        update_animated_instance_ubo(queue, &component->animation_instance);
    }
}

internal_function void push_entity_to_animated_queue(entity_t *e);
internal_function void push_entity_to_rolling_queue(entity_t *e);

internal_function void update_rendering_component(float32_t dt)
{
    for (uint32_t i = 0; i < g_entities->rendering_component_count; ++i)
    {
        struct rendering_component_t *component = &g_entities->rendering_components[ i ];
        entity_t *e = &g_entities->entity_list[ component->entity_index ];

        persist_var const matrix4_t CORRECTION_90 = glm::rotate(glm::radians(90.0f), vector3_t(0.0f, 1.0f, 0.0f));

        vector3_t view_dir = glm::normalize(e->ws_d);
        float32_t dir_x = view_dir.x;
        float32_t dir_z = view_dir.z;
        float32_t rotation_angle = atan2(dir_z, dir_x);

        matrix4_t rot_matrix = glm::rotate(-rotation_angle, vector3_t(0.0f, 1.0f, 0.0f));
        
        if (component->enabled)
        {
            if (e->on_t)
            {
                component->push_k.ws_t = glm::translate(e->ws_p) * CORRECTION_90 * rot_matrix * e->rolling_rotation * glm::scale(e->size);
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

        if (e->rolling_mode)
        {
            push_entity_to_rolling_queue(e);
        }
        else
        {
            push_entity_to_animated_queue(e);
        }
    }
}

uint32_t add_network_component(void)
{
    uint32_t component_index = g_entities->network_component_count++;
    return(component_index);
}

struct network_component_t *get_network_component(uint32_t index)
{
    return(&g_entities->network_components[index]);
}

internal_function struct physics_component_t *add_physics_component(entity_t *e, bool enabled)
{
    e->components.physics_component = g_entities->physics_component_count++;
    struct physics_component_t *component = &g_entities->physics_components[ e->components.physics_component ];
    component->entity_index = e->index;
    component->enabled = enabled;

    return(component);
}

internal_function void update_standing_entity_physics(struct physics_component_t *component, entity_t *e, uint32_t *action_flags, float32_t dt)
{
    // Not in rolling mode
    /*detected_collision_return_t precollision = detect_terrain_collision(&component->hitbox, e->size, e->ws_p, e->on_t);

    component->surface_normal = precollision.ws_normal;
                
    vector3_t sliding_down_dir = glm::normalize(get_sliding_down_direction(e->ws_d, e->on_t->ws_n, component->surface_normal));

    vector3_t result_force = vector3_t(0.0f);

    if (component->is_resting)
    {
        if (component->momentum > 0.8f)
        {
            e->is_sliding_not_rolling_mode = 1;
                    
            const float32_t TERRAIN_ROUGHNESS = 0.5f;
            float32_t cos_theta = glm::dot(-e->on_t->ws_n, -component->surface_normal);
            vector3_t friction_force = -e->ws_v * TERRAIN_ROUGHNESS * 9.81f * cos_theta;

            e->ws_v += friction_force * dt;
            component->momentum = glm::length(e->ws_v);

            if (component->momentum < 0.8f) component->momentum = 0.0f;
        }
        else
        {
            e->is_sliding_not_rolling_mode = 0;
            component->momentum = 0.0f;
            // Transition to jump
        }
    }

    vector3_t right = glm::cross(sliding_down_dir, e->on_t->ws_n);
    
    if (*action_flags & (1 << action_flags_t::ACTION_FORWARD)) result_force += sliding_down_dir;
    if (*action_flags & (1 << action_flags_t::ACTION_BACK)) result_force -= sliding_down_dir;
    if (*action_flags & (1 << action_flags_t::ACTION_RIGHT)) result_force += right;
    if (*action_flags & (1 << action_flags_t::ACTION_LEFT)) result_force -= right;

    if (*action_flags & (1 << action_flags_t::ACTION_DOWN)) e->is_sitting = 1;
    else e->is_sitting = 0;
                
    if (*action_flags && !(*action_flags & (1 << action_flags_t::ACTION_DOWN)))
    {
        result_force = glm::normalize(result_force);
        result_force -= e->on_t->ws_n;
        result_force = glm::normalize(result_force);
        if (*action_flags & (1 << action_flags_t::ACTION_RUN))
        {
            result_force *= 3.5f;
        }
    }
                
    const vector3_t gravity = -e->on_t->ws_n * 14.81f;

    e->ws_v += gravity * dt;

    detected_collision_return_t collision = detect_terrain_collision(&component->hitbox,
                                                                     e->size,
                                                                     e->ws_p + e->ws_v * dt + result_force * dt * 15.0f,
                                                                     e->on_t);

    component->surface_normal = collision.ws_normal;
                
    if (collision.detected)
    {
        component->is_resting = physics_component_t::is_resting_t::RESTING;
                    
        e->ws_p = collision.ws_at + e->size.y * e->on_t->ws_n;
        e->is_in_air = 0;

        vector3_t ts_v = matrix4_mul_vec3(e->on_t->inverse_transform, e->ws_v, WITHOUT_TRANSLATION);
        vector3_t ws_v = matrix4_mul_vec3(e->on_t->push_k.transform, vector3_t(ts_v.x, 0.0f, ts_v.z), WITHOUT_TRANSLATION);
        e->ws_v = ws_v;
    }
    else
    {
        component->is_resting = physics_component_t::is_resting_t::NOT_RESTING;
                    
        e->ws_p = e->ws_p + e->ws_v * dt;
        e->is_in_air = 1;
        }*/
}

internal_function void update_rolling_entity_physics(struct physics_component_t *component, entity_t *e, uint32_t *action_flags, float32_t dt)
{
    /*    bool hardcode_position = 0;

    if (component->is_resting == physics_component_t::is_resting_t::RESTING)
    {                    
        // Apply friction
        // TODO: Don't hardcode the roughness of the terrain surface
        const float32_t TERRAIN_ROUGHNESS = 0.5f;
        float32_t cos_theta = glm::dot(-e->on_t->ws_n, -component->surface_normal);
        vector3_t friction_force = -e->ws_v * TERRAIN_ROUGHNESS * 14.81f * cos_theta;

        e->ws_v += friction_force * dt;

        uint32_t *actions_flags = &e->action_flags;
        if (*action_flags & (1 << action_flags_t::ACTION_DOWN))
        {                        
            vector3_t sliding_down_dir = glm::normalize(get_sliding_down_direction(e->ws_d, e->on_t->ws_n, component->surface_normal));
            sliding_down_dir -= e->on_t->ws_n;
            sliding_down_dir = glm::normalize(sliding_down_dir);

            float32_t inclination_diff = glm::length(glm::cross(component->surface_normal, e->on_t->ws_n));
            inclination_diff += 1.0f;
            inclination_diff /= 2.0f;
            component->momentum += inclination_diff * dt * 8.0f;

            e->ws_v += component->momentum * sliding_down_dir;

            if (distance_squared(e->ws_v) < 100.0f)
            {
                hardcode_position = 1;
            }
        }

        e->rolling_rotation_angle += ((glm::length(e->ws_v) * dt) / calculate_sphere_circumference(e->size.x)) * 360.0f;
        e->rolling_rotation = glm::rotate(glm::radians(e->rolling_rotation_angle), vector3_t(1.0f, 0.0f, 0.0f));

        if (e->rolling_rotation_angle > 360.0f)
        {
            e->rolling_rotation_angle = e->rolling_rotation_angle - 360.0f;
        }
    }

    vector3_t gravity = -e->on_t->ws_n * 14.81f;
    if (component->is_resting == physics_component_t::is_resting_t::RESTING)
    {
        gravity *= 10.0f;
    }

    e->ws_v += gravity * dt;

    collide_and_slide_collision_t gravity_collision = detect_collision_against_possible_colliding_triangles(e->on_t,
                                                                                                            matrix4_mul_vec3(e->on_t->inverse_transform, e->ws_p, WITH_TRANSLATION),
                                                                                                            matrix4_mul_vec3(glm::scale(1.0f / e->on_t->size), e->size, TRANSLATION_DONT_CARE),
                                                                                                            matrix4_mul_vec3(e->on_t->inverse_transform, e->ws_v * dt, WITHOUT_TRANSLATION),
                                                                                                            dt,
                                                                                                            0, 1);

    collide_and_slide_collision_t collision = detect_collision_against_possible_colliding_triangles(e->on_t,
                                                                                                    matrix4_mul_vec3(e->on_t->inverse_transform, e->ws_p, WITH_TRANSLATION),
                                                                                                    matrix4_mul_vec3(glm::scale(1.0f / e->on_t->size), e->size, TRANSLATION_DONT_CARE),
                                                                                                    vector3_t(0.0f),
                                                                                                    dt,
                                                                                                    0,
                                                                                                    0);
            
    // If there was a collision project the velocity vector onto the terrain
    if (collision.collided)
    {
        vector3_t v = e->ws_v;
        vector3_t ws_normal = matrix4_mul_vec3(e->on_t->push_k.transform, glm::normalize(collision.ts_normal), WITHOUT_TRANSLATION);
        vector3_t sliding_dir = get_sliding_down_direction(v, e->on_t->ws_n, glm::normalize(ws_normal));

        vector3_t ws_new_sliding_direction;
        vector3_t projected;
                
        if (distance_squared(sliding_dir) < 0.0000001f)
        {
            ws_new_sliding_direction = vector3_t(0.0f);
            projected = vector3_t(0.0f);
        }
        else
        {
            ws_new_sliding_direction = glm::normalize(sliding_dir);
            projected = glm::proj(v, ws_new_sliding_direction);
        }

        e->ws_v = projected;

        component->surface_normal = glm::normalize(matrix4_mul_vec3(e->on_t->push_k.transform, collision.ts_normal, WITHOUT_TRANSLATION));
                
        // Is resting on the terrain (not in the air) 
        component->is_resting = physics_component_t::is_resting_t::RESTING;
    }
    else
    {
        float32_t ws_distance = get_position_distance_from_terrain(e->on_t,
                                                                   matrix4_mul_vec3(e->on_t->inverse_transform, e->ws_p, WITH_TRANSLATION),
                                                                   e->size);
        component->is_resting = physics_component_t::is_resting_t::NOT_RESTING;
    }

    e->ws_p = matrix4_mul_vec3(e->on_t->push_k.transform, gravity_collision.ts_position, WITH_TRANSLATION);*/
}

internal_function void update_physics_components(float32_t dt)
{
    for (uint32_t i = 0; i < g_entities->physics_component_count; ++i)
    {
        struct physics_component_t *component = &g_entities->physics_components[ i ];
        entity_t *e = &g_entities->entity_list[ component->entity_index ];
        uint32_t *action_flags = &e->action_flags;

        if (component->enabled)
        {
            if (e->rolling_mode)
            {
                update_rolling_entity_physics(component, e, action_flags, dt);
            }
            else
            {
                update_standing_entity_physics(component, e, action_flags, dt);
            }
        }
    }
}

internal_function entity_handle_t add_entity(const entity_t &e)

{
    entity_handle_t view;
    view = g_entities->entity_count;

    g_entities->name_map.insert(e.id.hash, view);
    
    g_entities->entity_list[g_entities->entity_count++] = e;

    auto e_ptr = get_entity(view);
    e_ptr->rolling_mode = 0;
    e_ptr->index = view;

    return(view);
}

internal_function void make_entity_instanced_renderable(model_handle_t model_handle, const constant_string_t &e_mtrl_name)
{
    // TODO(luc) : first need to add support for instance rendering in material renderers.
}

internal_function void update_entities(float32_t dt, application_type_t app_type)
{
    switch (app_type)
    {
    case application_type_t::WINDOW_APPLICATION_MODE:
        {
        update_physics_components(dt);
        update_camera_components(dt);

        update_rendering_component(dt);
        update_animation_component(dt);
        } break;
    case application_type_t::CONSOLE_APPLICATION_MODE:
        {
            update_physics_components(dt);
        } break;
    }
}

void make_entity_main(entity_handle_t entity_handle, input_state_t *input_state)
{
    entity_t *entity = get_entity(entity_handle);

    camera_component_t *camera_component_ptr = add_camera_component(entity, add_camera(input_state, get_backbuffer_resolution()));
    camera_component_ptr->is_third_person = true;
        
    bind_camera_to_3d_scene_output(camera_component_ptr->camera);

    g_entities->main_entity = entity_handle;
}

void make_entity_renderable(entity_handle_t entity_handle, entity_color_t color)
{
    entity_t *entity_ptr = get_entity(entity_handle);
    
    rendering_component_t *entity_ptr_rendering = add_rendering_component(entity_ptr);
    animation_component_t *entity_animation = add_animation_component(entity_ptr,
                                                                      g_uniform_layout_manager->get(g_uniform_layout_manager->get_handle("uniform_layout.joint_ubo"_hash)),
                                                                      &g_entities->entity_mesh_skeleton,
                                                                      &g_entities->entity_mesh_cycles,
                                                                      get_global_command_pool());

    persist_var vector4_t colors[entity_color_t::INVALID_COLOR] = { vector4_t(0.0f, 0.0f, 0.7f, 1.0f),
                                                                    vector4_t(0.7f, 0.0f, 0.0f, 1.0f),
                                                                    vector4_t(0.4f, 0.4f, 0.4f, 1.0f),
                                                                    vector4_t(0.1f, 0.1f, 0.1f, 1.0f),
                                                                    vector4_t(0.0f, 0.7f, 0.0f, 1.0f) };
        
    entity_ptr_rendering->push_k.color = colors[color];
    entity_ptr_rendering->push_k.roughness = 0.8f;
    entity_ptr_rendering->push_k.metalness = 0.6f;
}

internal_function void initialize_entities_graphics_data(VkCommandPool *cmdpool, input_state_t *input_state)
{
    g_entities->rolling_entity_mesh = load_mesh(mesh_file_format_t::CUSTOM_MESH, "models/icosphere.mesh_custom", cmdpool);
    g_entities->rolling_entity_model = make_mesh_attribute_and_binding_information(&g_entities->rolling_entity_mesh);
    g_entities->rolling_entity_model.index_data = g_entities->rolling_entity_mesh.index_data;
    
    g_entities->entity_mesh = load_mesh(mesh_file_format_t::CUSTOM_MESH, "models/spaceman.mesh_custom", cmdpool);
    g_entities->entity_model = make_mesh_attribute_and_binding_information(&g_entities->entity_mesh);
    g_entities->entity_model.index_data = g_entities->entity_mesh.index_data;
    g_entities->entity_mesh_skeleton = load_skeleton("models/spaceman_walk.skeleton_custom");
    g_entities->entity_mesh_cycles = load_animations("models/spaceman.animations_custom");

    uniform_layout_handle_t animation_layout_hdl = g_uniform_layout_manager->add("uniform_layout.joint_ubo"_hash);
    uniform_layout_t *animation_layout_ptr = g_uniform_layout_manager->get(animation_layout_hdl);
    uniform_layout_info_t animation_ubo_info = {};
    animation_ubo_info.push(1, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT);
    *animation_layout_ptr = make_uniform_layout(&animation_ubo_info);
    
    g_entities->entity_ppln = g_pipeline_manager->add("pipeline.model"_hash);
    auto *entity_ppln = g_pipeline_manager->get(g_entities->entity_ppln);
    {
        graphics_pipeline_info_t *info = (graphics_pipeline_info_t *)allocate_free_list(sizeof(graphics_pipeline_info_t));
        render_pass_handle_t dfr_render_pass = g_render_pass_manager->get_handle("render_pass.deferred_render_pass"_hash);
        shader_modules_t modules(shader_module_info_t{"shaders/SPV/lp_notex_animated.vert.spv", VK_SHADER_STAGE_VERTEX_BIT},
                               shader_module_info_t{"shaders/SPV/lp_notex_animated.geom.spv", VK_SHADER_STAGE_GEOMETRY_BIT},
                               shader_module_info_t{"shaders/SPV/lp_notex_animated.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT});
        shader_uniform_layouts_t layouts(g_uniform_layout_manager->get_handle("uniform_layout.camera_transforms_ubo"_hash),
                                         g_uniform_layout_manager->get_handle("descriptor_set_layout.2D_sampler_layout"_hash),
                                         animation_layout_hdl);
        shader_pk_data_t push_k = {160, 0, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_GEOMETRY_BIT };
        shader_blend_states_t blending(false, false, false, false);
        dynamic_states_t dynamic(VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_LINE_WIDTH);
        fill_graphics_pipeline_info(modules, VK_FALSE, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_POLYGON_MODE_FILL,
                                    VK_CULL_MODE_NONE, layouts, push_k, get_backbuffer_resolution(), blending, &g_entities->entity_model,
                                    true, 0.0f, dynamic, g_render_pass_manager->get(dfr_render_pass), 0, info);
        entity_ppln->info = info;
        make_graphics_pipeline(entity_ppln);
    }
    // TODO: Rename all the pipelines correctly : animated / normal
    g_entities->rolling_entity_ppln = g_pipeline_manager->add("pipeline.ball"_hash);
    auto *rolling_entity_ppln = g_pipeline_manager->get(g_entities->rolling_entity_ppln);
    {
        graphics_pipeline_info_t *info = (graphics_pipeline_info_t *)allocate_free_list(sizeof(graphics_pipeline_info_t));
        render_pass_handle_t dfr_render_pass = g_render_pass_manager->get_handle("render_pass.deferred_render_pass"_hash);
        shader_modules_t modules(shader_module_info_t{"shaders/SPV/lp_notex_model.vert.spv", VK_SHADER_STAGE_VERTEX_BIT},
                                 shader_module_info_t{"shaders/SPV/lp_notex_model.geom.spv", VK_SHADER_STAGE_GEOMETRY_BIT},
                                 shader_module_info_t{"shaders/SPV/lp_notex_model.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT});
        shader_uniform_layouts_t layouts(g_uniform_layout_manager->get_handle("uniform_layout.camera_transforms_ubo"_hash),
                                         g_uniform_layout_manager->get_handle("descriptor_set_layout.2D_sampler_layout"_hash));
        shader_pk_data_t push_k = {160, 0, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_GEOMETRY_BIT };
        shader_blend_states_t blending(false, false, false, false);
        dynamic_states_t dynamic(VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_LINE_WIDTH);
        fill_graphics_pipeline_info(modules, VK_FALSE, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_POLYGON_MODE_FILL,
                                    VK_CULL_MODE_NONE, layouts, push_k, get_backbuffer_resolution(), blending, &g_entities->rolling_entity_model,
                                    true, 0.0f, dynamic, g_render_pass_manager->get(dfr_render_pass), 0, info);
        rolling_entity_ppln->info = info;
        make_graphics_pipeline(rolling_entity_ppln);
    }

    g_entities->dbg_hitbox_ppln = g_pipeline_manager->add("pipeline.hitboxes"_hash);
    auto *hitbox_ppln = g_pipeline_manager->get(g_entities->dbg_hitbox_ppln);
    {
        graphics_pipeline_info_t *info = (graphics_pipeline_info_t *)allocate_free_list(sizeof(graphics_pipeline_info_t));
        render_pass_handle_t dfr_render_pass = g_render_pass_manager->get_handle("render_pass.deferred_render_pass"_hash);
        shader_modules_t modules(shader_module_info_t{"shaders/SPV/hitbox_render.vert.spv", VK_SHADER_STAGE_VERTEX_BIT},
                                 shader_module_info_t{"shaders/SPV/hitbox_render.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT});
        shader_uniform_layouts_t layouts(g_uniform_layout_manager->get_handle("uniform_layout.camera_transforms_ubo"_hash));
        shader_pk_data_t push_k = {240, 0, VK_SHADER_STAGE_VERTEX_BIT};
        shader_blend_states_t blending(false, false, false, false);
        dynamic_states_t dynamic(VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_LINE_WIDTH);
        fill_graphics_pipeline_info(modules, VK_FALSE, VK_PRIMITIVE_TOPOLOGY_LINE_LIST, VK_POLYGON_MODE_LINE,
                                    VK_CULL_MODE_NONE, layouts, push_k, get_backbuffer_resolution(), blending, nullptr,
                                    true, 0.0f, dynamic, g_render_pass_manager->get(dfr_render_pass), 0, info);
        hitbox_ppln->info = info;
        make_graphics_pipeline(hitbox_ppln);
    }

    g_entities->entity_shadow_ppln = g_pipeline_manager->add("pipeline.model_shadow"_hash);
    auto *entity_shadow_ppln = g_pipeline_manager->get(g_entities->entity_shadow_ppln);
    {
        graphics_pipeline_info_t *info = (graphics_pipeline_info_t *)allocate_free_list(sizeof(graphics_pipeline_info_t));
        auto shadow_display = get_shadow_display();
        VkExtent2D shadow_extent {shadow_display.shadowmap_w, shadow_display.shadowmap_h};
        render_pass_handle_t shadow_render_pass = g_render_pass_manager->get_handle("render_pass.shadow_render_pass"_hash);
        shader_modules_t modules(shader_module_info_t{"shaders/SPV/lp_notex_model_shadow.vert.spv", VK_SHADER_STAGE_VERTEX_BIT},
                               shader_module_info_t{"shaders/SPV/lp_notex_model_shadow.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT});
        shader_uniform_layouts_t layouts(g_uniform_layout_manager->get_handle("uniform_layout.camera_transforms_ubo"_hash),
                                         animation_layout_hdl);
        shader_pk_data_t push_k = {160, 0, VK_SHADER_STAGE_VERTEX_BIT};
        shader_blend_states_t blending(false);
        dynamic_states_t dynamic(VK_DYNAMIC_STATE_DEPTH_BIAS, VK_DYNAMIC_STATE_VIEWPORT);
        fill_graphics_pipeline_info(modules, VK_FALSE, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_POLYGON_MODE_FILL,
                               VK_CULL_MODE_NONE, layouts, push_k, shadow_extent, blending, &g_entities->entity_model,
                               true, 0.0f, dynamic, g_render_pass_manager->get(shadow_render_pass), 0, info);
        entity_shadow_ppln->info = info;
        make_graphics_pipeline(entity_shadow_ppln);
    }

    g_entities->rolling_entity_shadow_ppln = g_pipeline_manager->add("pipeline.ball_shadow"_hash);
    auto *rolling_entity_shadow_ppln = g_pipeline_manager->get(g_entities->rolling_entity_shadow_ppln);
    {
        graphics_pipeline_info_t *info = (graphics_pipeline_info_t *)allocate_free_list(sizeof(graphics_pipeline_info_t));
        auto shadow_display = get_shadow_display();
        VkExtent2D shadow_extent {shadow_display.shadowmap_w, shadow_display.shadowmap_h};
        render_pass_handle_t shadow_render_pass = g_render_pass_manager->get_handle("render_pass.shadow_render_pass"_hash);
        shader_modules_t modules(shader_module_info_t{"shaders/SPV/model_shadow.vert.spv", VK_SHADER_STAGE_VERTEX_BIT},
                                 shader_module_info_t{"shaders/SPV/model_shadow.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT});
        shader_uniform_layouts_t layouts(g_uniform_layout_manager->get_handle("uniform_layout.camera_transforms_ubo"_hash));
        shader_pk_data_t push_k = {160, 0, VK_SHADER_STAGE_VERTEX_BIT};
        shader_blend_states_t blending(false);
        dynamic_states_t dynamic(VK_DYNAMIC_STATE_DEPTH_BIAS, VK_DYNAMIC_STATE_VIEWPORT);
        fill_graphics_pipeline_info(modules, VK_FALSE, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_POLYGON_MODE_FILL,
                                    VK_CULL_MODE_NONE, layouts, push_k, shadow_extent, blending, &g_entities->rolling_entity_model,
                                    true, 0.0f, dynamic, g_render_pass_manager->get(shadow_render_pass), 0, info);
        rolling_entity_shadow_ppln->info = info;
        make_graphics_pipeline(rolling_entity_shadow_ppln);
    }
}

internal_function void initialize_entities_data(VkCommandPool *cmdpool, input_state_t *input_state, application_type_t app_type)
{
    entity_t r2 = construct_entity("main"_hash,
                                   vector3_t(0.0f),
                                   vector3_t(1.0f, 0.0f, 1.0f),
                                   quaternion_t(glm::radians(45.0f), vector3_t(0.0f, 1.0f, 0.0f)));

    r2.size = vector3_t(5.0f);
    //r2.ws_v = vector3_t(0.0f, 0.0f, -20.0f);
    entity_handle_t rv2 = add_entity(r2);
    g_entities->main_entity = rv2;
    auto *r2_ptr = get_entity(rv2);

    if (app_type == application_type_t::WINDOW_APPLICATION_MODE)
    {
        rendering_component_t *r2_ptr_rendering = add_rendering_component(r2_ptr);
        animation_component_t *r2_animation = add_animation_component(r2_ptr,
                                                                      g_uniform_layout_manager->get(g_uniform_layout_manager->get_handle("uniform_layout.joint_ubo"_hash)),
                                                                      &g_entities->entity_mesh_skeleton,
                                                                      &g_entities->entity_mesh_cycles,
                                                                      cmdpool);

        r2_ptr_rendering->push_k.color = vector4_t(0.7f, 0.7f, 0.7f, 1.0f);
        r2_ptr_rendering->push_k.roughness = 0.8f;
        r2_ptr_rendering->push_k.metalness = 0.6f;

        auto *camera_component_ptr = add_camera_component(r2_ptr, add_camera(input_state, get_backbuffer_resolution()));
        camera_component_ptr->is_third_person = true;
        
        bind_camera_to_3d_scene_output(camera_component_ptr->camera);
    }

    physics_component_t *physics = add_physics_component(r2_ptr, false);
    physics->enabled = true;
    physics->hitbox.x_min = -1.001f;
    physics->hitbox.x_max = 1.001f;
    physics->hitbox.y_min = -1.001f;
    physics->hitbox.y_max = 1.001f;
    physics->hitbox.z_min = -1.001f;
    physics->hitbox.z_max = 1.001f;
}

internal_function void dbg_render_hitboxes(uniform_group_t *transforms_ubo, gpu_command_queue_t *queue)
{
    if (g_entities->dbg.hit_box_display)
    {
        auto *dbg_hitbox_ppln = g_pipeline_manager->get(g_entities->dbg_hitbox_ppln);
        command_buffer_bind_pipeline(&dbg_hitbox_ppln->pipeline, &queue->q);

        command_buffer_bind_descriptor_sets(&dbg_hitbox_ppln->layout, {1, transforms_ubo}, &queue->q);

        for (uint32_t i = 0; i < g_entities->physics_component_count; ++i)
        {
            struct push_k_t
            {
                alignas(16) matrix4_t model_matrix;
                alignas(16) vector4_t positions[8];
                alignas(16) vector4_t color;
            } pk;
            physics_component_t *physics_component = &g_entities->physics_components[i];
            entity_t *entity = get_entity(physics_component->entity_index);

            if (entity->index != g_entities->main_entity)
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

                command_buffer_push_constant(&pk, sizeof(pk), 0, VK_SHADER_STAGE_VERTEX_BIT, dbg_hitbox_ppln->layout, &queue->q);

                command_buffer_draw(&queue->q, 24, 1, 0, 0);
            }
        }
    }
}

internal_function void render_world(uint32_t image_index, uint32_t current_frame, gpu_command_queue_t *queue)
{
    // Fetch some data needed to render
    auto transforms_ubo_uniform_groups = get_camera_transform_uniform_groups();
    shadow_display_t shadow_display_data = get_shadow_display();
    
    uniform_group_t uniform_groups[2] = {transforms_ubo_uniform_groups[image_index], shadow_display_data.texture};

    camera_t *camera = get_camera_bound_to_3d_output();
    
    // Rendering to the shadow map
    begin_shadow_offscreen(4000, 4000, queue);
    {
        auto *model_ppln = g_pipeline_manager->get(g_entities->entity_shadow_ppln);

        g_world_submission_queues[ENTITY_QUEUE].submit_queued_materials({1, &transforms_ubo_uniform_groups[image_index]}, model_ppln, queue, VK_COMMAND_BUFFER_LEVEL_PRIMARY);

        auto *rolling_model_ppln = g_pipeline_manager->get(g_entities->rolling_entity_shadow_ppln);

        g_world_submission_queues[ROLLING_ENTITY_QUEUE].submit_queued_materials({1, &transforms_ubo_uniform_groups[image_index]}, rolling_model_ppln, queue, VK_COMMAND_BUFFER_LEVEL_PRIMARY);
    }
    end_shadow_offscreen(queue);

    // Rendering the scene with lighting and everything
    begin_deferred_rendering(image_index, queue);
    {
        auto *entity_ppln = g_pipeline_manager->get(g_entities->entity_ppln);
        auto *rolling_entity_ppln = g_pipeline_manager->get(g_entities->rolling_entity_ppln);
    
        g_world_submission_queues[ENTITY_QUEUE].submit_queued_materials({2, uniform_groups}, entity_ppln, queue, VK_COMMAND_BUFFER_LEVEL_PRIMARY);
        g_world_submission_queues[ROLLING_ENTITY_QUEUE].submit_queued_materials({2, uniform_groups}, rolling_entity_ppln, queue, VK_COMMAND_BUFFER_LEVEL_PRIMARY);

        g_world_submission_queues[ENTITY_QUEUE].flush_queue();
        g_world_submission_queues[ROLLING_ENTITY_QUEUE].flush_queue();
        
        render_3d_frustum_debug_information(&uniform_groups[0], queue, image_index, g_pipeline_manager->get(g_entities->dbg_hitbox_ppln));
        dbg_render_hitboxes(&uniform_groups[0], queue);
        
        // ---- render skybox ----
        render_atmosphere({1, uniform_groups}, camera->p, queue);
    }
    end_deferred_rendering(camera->v_m, queue);

    apply_pfx_on_scene(queue, &transforms_ubo_uniform_groups[image_index], camera->v_m, camera->p_m);
}

internal_function int32_t lua_get_player_position(lua_State *state);
internal_function int32_t lua_set_player_position(lua_State *state);
internal_function int32_t lua_toggle_collision_box_render(lua_State *state);
internal_function int32_t lua_toggle_collision_edge_render(lua_State *state);
internal_function int32_t lua_toggle_sphere_collision_triangles_render(lua_State *state);
internal_function int32_t lua_render_entity_direction_information(lua_State *state);
internal_function int32_t lua_set_veclocity_in_view_direction(lua_State *state);
internal_function int32_t lua_get_player_ts_view_direction(lua_State *state);
internal_function int32_t lua_stop_simulation(lua_State *state);
internal_function int32_t lua_load_mesh(lua_State *state);
internal_function int32_t lua_load_model_information_for_mesh(lua_State *state);
internal_function int32_t lua_load_skeleton(lua_State *state);
internal_function int32_t lua_load_animations(lua_State *state);
internal_function int32_t lua_initialize_entity(lua_State *state);
internal_function int32_t lua_attach_rendering_component(lua_State *state);
internal_function int32_t lua_attach_animation_component(lua_State *state);
internal_function int32_t lua_attach_physics_component(lua_State *state);
internal_function int32_t lua_attach_camera_component(lua_State *state);
internal_function int32_t lua_bind_entity_to_3d_output(lua_State *state);
internal_function int32_t lua_go_down(lua_State *state);
internal_function int32_t lua_placeholder_c_out(lua_State *state) { return(0); }
internal_function int32_t lua_reinitialize(lua_State *state);

internal_function void entry_point(void)
{
    // Load globals
    execute_lua("globals = require \"scripts/globals/globals\"");
    
    // Load startup code
    const char *startup_script = "scripts/sandbox/startup.lua";
    file_handle_t handle = create_file(startup_script, file_type_t::TEXT);
    auto contents = read_file_tmp(handle);
    execute_lua((const char *)contents.content);
    remove_and_destroy_file(handle);

    // Execute startup code
    execute_lua("startup()");
}

void initialize_world(input_state_t *input_state, VkCommandPool *cmdpool, application_type_t app_type, application_mode_t app_mode)
{
    add_global_to_lua(script_primitive_type_t::FUNCTION, "get_player_position", &lua_get_player_position);
    add_global_to_lua(script_primitive_type_t::FUNCTION, "set_player_position", &lua_set_player_position);
    add_global_to_lua(script_primitive_type_t::FUNCTION, "toggle_hit_box_display", &lua_toggle_collision_box_render);
    add_global_to_lua(script_primitive_type_t::FUNCTION, "render_direction_info", &lua_render_entity_direction_information);
    add_global_to_lua(script_primitive_type_t::FUNCTION, "get_ts_view_dir", &lua_get_player_ts_view_direction);
    add_global_to_lua(script_primitive_type_t::FUNCTION, "stop_simulation", &lua_stop_simulation);
    add_global_to_lua(script_primitive_type_t::FUNCTION, "go_down", &lua_go_down);
    if (app_type == application_type_t::CONSOLE_APPLICATION_MODE)
    {
        add_global_to_lua(script_primitive_type_t::FUNCTION, "c_out", &lua_placeholder_c_out);
    }

    if (app_type == application_type_t::WINDOW_APPLICATION_MODE)
    {
        g_world_submission_queues[ROLLING_ENTITY_QUEUE] = make_gpu_material_submission_queue(10,
                                                                                             VK_SHADER_STAGE_VERTEX_BIT,
                                                                                             VK_COMMAND_BUFFER_LEVEL_SECONDARY,
                                                                                             cmdpool);
    
        g_world_submission_queues[ENTITY_QUEUE] = make_gpu_material_submission_queue(20,
                                                                                     VK_SHADER_STAGE_VERTEX_BIT,
                                                                                     VK_COMMAND_BUFFER_LEVEL_SECONDARY,
                                                                                     cmdpool);
    }
    
    // Creation of terrains, entities, etc...
    if (app_mode == application_mode_t::SERVER_MODE)
    {
        //entry_point();
    }
    
    // Rendering data, queues, etc...
    if (app_type == application_type_t::WINDOW_APPLICATION_MODE)
    {
        // This wouldnot happen if the user wants just to run a server without displaying anything to a graphics context
        //initialize_graphics_terrain_data(cmdpool);
        initialize_entities_graphics_data(cmdpool, input_state);
    }
    // In future, this should only happen in a .lua script file
    //initialize_entities_data(cmdpool, input_state, app_type);

    clear_linear();

    update_network_world_state();
}

internal_function void clean_up_entities(void)
{
    // Gets rid of all the entities, terrains, etc..., but not rendering stuff.
    g_entities->entity_count = 0;
    g_entities->physics_component_count = 0;
    g_entities->camera_component_count = 0;
    g_entities->rendering_component_count = 0;

    for (uint32_t i = 0; i < g_entities->animation_component_count; ++i)
    {
        destroy_animated_instance(&g_entities->animation_components[i].animation_instance);
    }
    g_entities->animation_component_count = 0;
    
    g_entities->main_entity = -1;

    g_entities->name_map.clean_up();

    g_world_submission_queues[TERRAIN_QUEUE].mtrl_count = 0;
    g_world_submission_queues[ROLLING_ENTITY_QUEUE].mtrl_count = 0;
}

void clean_up_world_data(void)
{
    clean_up_entities();
}

void make_world_data(void /* Some kind of state */)
{
    
}

void update_network_world_state(void)
{
    
    
    // Initialize entities part of the world state
}

network_world_state_t *get_network_world_state(void)
{
    return(g_network_world_state);
}

void sync_gpu_memory_with_world_state(gpu_command_queue_t *cmdbuf, uint32_t image_index)
{
    update_animation_gpu_data(cmdbuf);
    update_3d_output_camera_transforms(image_index);
}

void handle_all_input(input_state_t *input_state, float32_t dt, element_focus_t focus)
{
    // If world has focus
    if (focus == WORLD_3D_ELEMENT_FOCUS)
    {
        handle_world_input(input_state, dt);
        handle_input_debug(input_state, dt);
    }
}

void update_world(input_state_t *input_state,
                  float32_t dt,
                  uint32_t image_index,
                  uint32_t current_frame,
                  gpu_command_queue_t *cmdbuf,
                  application_type_t app_type,
                  element_focus_t focus)
{    
    switch (app_type)
    {
    case application_type_t::WINDOW_APPLICATION_MODE:
        {
            handle_all_input(input_state, dt, focus);
            
            update_entities(dt, app_type);

            sync_gpu_memory_with_world_state(cmdbuf, image_index);
    
            render_world(image_index, current_frame, cmdbuf);
        } break;
    case application_type_t::CONSOLE_APPLICATION_MODE:
        {
            update_entities(dt, app_type);
        } break;
    }
}


#include <glm/gtx/string_cast.hpp>

void handle_main_entity_mouse_movement(entity_t *e, uint32_t *action_flags, input_state_t *input_state, float32_t dt)
{
    if (input_state->cursor_moved)
    {
        vector3_t up = vector3_t(0.0f, 1.0f, 0.0f);
        
        // TODO: Make sensitivity configurable with a file or something, and later menu
        persist_var constexpr uint32_t SENSITIVITY = 15.0f;
    
        vector2_t prev_mp = vector2_t(input_state->previous_cursor_pos_x, input_state->previous_cursor_pos_y);
        vector2_t curr_mp = vector2_t(input_state->cursor_pos_x, input_state->cursor_pos_y);

        vector3_t res = e->ws_d;
	    
        vector2_t d = (curr_mp - prev_mp);

        float32_t x_angle = glm::radians(-d.x) * SENSITIVITY * dt;// *elapsed;
        float32_t y_angle = glm::radians(-d.y) * SENSITIVITY * dt;// *elapsed;
                
        res = matrix3_t(glm::rotate(x_angle, up)) * res;
        vector3_t rotate_y = glm::cross(res, up);
        res = matrix3_t(glm::rotate(y_angle, rotate_y)) * res;

        res = glm::normalize(res);
                
        float32_t up_dot_view = glm::dot(up, res);
        float32_t minus_up_dot_view = glm::dot(-up, res);
                

        float32_t limit = 0.99f;
        if (up_dot_view > -limit && up_dot_view < limit && minus_up_dot_view > -limit && minus_up_dot_view < limit)
        {
            e->ws_d = res;

            char buffer[40] = {};
            sprintf(buffer, "%f, %f\n", up_dot_view, minus_up_dot_view);
            OutputDebugString(buffer);
        }
        else
        {
            OutputDebugString("Too far\n");
        }
    }
}

void handle_main_entity_mouse_button_input(entity_t *e, uint32_t *action_flags, input_state_t *input_state, float32_t dt)
{
}

void handle_main_entity_keyboard_input(entity_t *e, uint32_t *action_flags, physics_component_t *e_physics, input_state_t *input_state, float32_t dt)
{
    vector3_t up = vector3_t(0.0f, 1.0f, 0.0f);
    
    uint32_t movements = 0;
    float32_t accelerate = 1.0f;
    
    auto acc_v = [&movements, &accelerate](const vector3_t &d, vector3_t &dst){ ++movements; dst += d * accelerate; };

    vector3_t d = glm::normalize(vector3_t(e->ws_d.x,
                                           e->ws_d.y,
                                           e->ws_d.z));

    vector3_t res = {};

    *action_flags = 0;
    if (input_state->keyboard[keyboard_button_type_t::R].is_down) {accelerate = 6.0f; *action_flags |= (1 << action_flags_t::ACTION_RUN);}
    if (input_state->keyboard[keyboard_button_type_t::W].is_down) {acc_v(d, res); *action_flags |= (1 << action_flags_t::ACTION_FORWARD);}
    if (input_state->keyboard[keyboard_button_type_t::A].is_down) {acc_v(-glm::cross(d, up), res); *action_flags |= (1 << action_flags_t::ACTION_LEFT);}
    if (input_state->keyboard[keyboard_button_type_t::S].is_down) {acc_v(-d, res); *action_flags |= (1 << action_flags_t::ACTION_BACK);} 
    if (input_state->keyboard[keyboard_button_type_t::D].is_down) {acc_v(glm::cross(d, up), res); *action_flags |= (1 << action_flags_t::ACTION_RIGHT);}
    
    if (input_state->keyboard[keyboard_button_type_t::SPACE].is_down)
    {
        acc_v(up, res);
    }
    
    if (input_state->keyboard[keyboard_button_type_t::LEFT_SHIFT].is_down)
    {
        acc_v(-up, res);
        *action_flags |= (1 << action_flags_t::ACTION_DOWN);
    }

    if (input_state->keyboard[keyboard_button_type_t::E].is_down && !e->toggled_rolling_previous_frame)
    {
        e->toggled_rolling_previous_frame = 1;
        e->rolling_mode ^= 1;
        if (!e->rolling_mode)
        {
            e->rolling_rotation = matrix4_t(1.0f);
            e->rolling_rotation_angle = 0.0f;
        }
    }
    else if (!input_state->keyboard[keyboard_button_type_t::E].is_down)
    {
        e->toggled_rolling_previous_frame = 0;
    }
            

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

void handle_main_entity_action(input_state_t *input_state, float32_t dt)
{
    entity_t *main_entity = get_main_entity();
    if (main_entity)
    {
        entity_t *e = main_entity;
        physics_component_t *e_physics = &g_entities->physics_components[e->components.physics_component];

        handle_main_entity_mouse_movement(e, &e->action_flags, input_state, dt);
        handle_main_entity_mouse_button_input(e, &e->action_flags, input_state, dt);
        handle_main_entity_keyboard_input(e, &e->action_flags, e_physics, input_state, dt);
    }
}

void handle_world_input(input_state_t *input_state, float32_t dt)
{
    handle_main_entity_action(input_state, dt);
}

// Not to do with moving the entity, just debug stuff : will be used later for stuff like opening menus
void handle_input_debug(input_state_t *input_state, float32_t dt)
{
    // ---- get bound entity ----
    // TODO make sure to check if main_entity < 0
    /*entity_t *e_ptr = &g_entities->entity_list[g_entities->main_entity];
      camera_component_t *e_camera_component = &g_entities->camera_components[e_ptr->components.camera_component];
      physics_component_t *e_physics = &g_entities->physics_components[e_ptr->components.physics_component];
      camera_t *e_camera = get_camera(e_camera_component->camera);
      vector3_t up = e_ptr->on_t->ws_n;
    
      shadow_matrices_t shadow_data = get_shadow_matrices();
      shadow_debug_t    shadow_debug = get_shadow_debug();
    
      //    shadow_data.light_view_matrix = glm::lookAt(vector3_t(0.0f), -glm::normalize(light_pos), vector3_t(0.0f, 1.0f, 0.0f));

      if (input_state->keyboard[keyboard_button_type_t::P].is_down)
      {
      for (uint32_t i = 0; i < 8; ++i)
      {
      e_camera->captured_frustum_corners[i] = shadow_debug.frustum_corners[i];
      }

      e_camera->captured = 1;
      e_camera->captured_shadow_corners[0] = vector4_t(shadow_debug.x_min, shadow_debug.y_max, shadow_debug.z_min, 1.0f);
      e_camera->captured_shadow_corners[1] = vector4_t(shadow_debug.x_max, shadow_debug.y_max, shadow_debug.z_min, 1.0f);
      e_camera->captured_shadow_corners[2] = vector4_t(shadow_debug.x_max, shadow_debug.y_min, shadow_debug.z_min, 1.0f);
      e_camera->captured_shadow_corners[3] = vector4_t(shadow_debug.x_min, shadow_debug.y_min, shadow_debug.z_min, 1.0f);

      e_camera->captured_shadow_corners[4] = vector4_t(shadow_debug.x_min, shadow_debug.y_max, shadow_debug.z_max, 1.0f);
      e_camera->captured_shadow_corners[5] = vector4_t(shadow_debug.x_max, shadow_debug.y_max, shadow_debug.z_max, 1.0f);
      e_camera->captured_shadow_corners[6] = vector4_t(shadow_debug.x_max, shadow_debug.y_min, shadow_debug.z_max, 1.0f);
      e_camera->captured_shadow_corners[7] = vector4_t(shadow_debug.x_min, shadow_debug.y_min, shadow_debug.z_max, 1.0f);
      }*/
}



void destroy_world(void)
{
    g_render_pass_manager->clean_up();
    g_image_manager->clean_up();
    g_framebuffer_manager->clean_up();
    g_pipeline_manager->clean_up();
    g_gpu_buffer_manager->clean_up();

    destroy_graphics();
}

internal_function int32_t lua_get_player_position(lua_State *state)
{
    // For now, just sets the main player's position
    entity_t *main_entity = &g_entities->entity_list[g_entities->main_entity];
    lua_pushnumber(state, main_entity->ws_p.x);
    lua_pushnumber(state, main_entity->ws_p.y);
    lua_pushnumber(state, main_entity->ws_p.z);
    return(3);
}

internal_function int32_t lua_set_player_position(lua_State *state)
{
    float32_t x = lua_tonumber(state, -3);
    float32_t y = lua_tonumber(state, -2);
    float32_t z = lua_tonumber(state, -1);
    entity_t *main_entity = &g_entities->entity_list[g_entities->main_entity];
    main_entity->ws_p.x = x;
    main_entity->ws_p.y = y;
    main_entity->ws_p.z = z;
    return(0);
}

internal_function int32_t lua_toggle_collision_box_render(lua_State *state)
{
    g_entities->dbg.hit_box_display ^= true;
    return(0);
}

internal_function int32_t lua_render_entity_direction_information(lua_State *state)
{
    const char *name = lua_tostring(state, -1);
    constant_string_t kname = make_constant_string(name, strlen(name));

    g_entities->dbg.render_sliding_vector_entity = get_entity(kname);

    persist_var char buffer[50];
    sprintf(buffer, "rendering for entity: %s", name);
    console_out(buffer);
    
    return(0);
}

internal_function int32_t lua_set_veclocity_in_view_direction(lua_State *state)
{
    const char *name = lua_tostring(state, -2);
    float32_t velocity = lua_tonumber(state, -1);
    constant_string_t kname = make_constant_string(name, strlen(name));
    entity_t *entity = get_entity(kname);
    entity->ws_v += entity->ws_d * velocity;
    return(0);
}

internal_function int32_t lua_get_player_ts_view_direction(lua_State *state)
{
    // For now, just sets the main player's position
    entity_t *main_entity = &g_entities->entity_list[g_entities->main_entity];
    //    vector4_t dir = glm::scale(main_entity->on_t->size) * main_entity->on_t->inverse_transform * vector4_t(main_entity->ws_d, 0.0f);
    lua_pushnumber(state, main_entity->ws_d.x);
    lua_pushnumber(state, main_entity->ws_d.y);
    lua_pushnumber(state, main_entity->ws_d.z);
    return(3);
}

internal_function int32_t lua_stop_simulation(lua_State *state)
{
    const char *name = lua_tostring(state, -1);
    constant_string_t kname = make_constant_string(name, strlen(name));

    entity_t *entity = get_entity(kname);

    physics_component_t *component = &g_entities->physics_components[ entity->components.physics_component ];
    component->enabled = false;
    component->ws_velocity = vector3_t(0.0f);
    
    return(0);
}

internal_function int32_t lua_go_down(lua_State *state)
{
    entity_t *main = get_main_entity();
    auto *istate = get_input_state();
    istate->keyboard[keyboard_button_type_t::LEFT_SHIFT].is_down = is_down_t::REPEAT;
    istate->keyboard[keyboard_button_type_t::LEFT_SHIFT].down_amount += 1.0f / 60.0f;
    return(0);
}

void initialize_world_translation_unit(struct game_memory_t *memory)
{
    g_entities = &memory->world_state.entities;
    g_network_world_state = &memory->world_state.network_world_state;
    g_world_submission_queues = memory->world_state.material_submission_queues;
}

internal_function int32_t lua_reinitialize(lua_State *state)
{
    return(0);
}

