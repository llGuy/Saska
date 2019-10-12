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
global_var struct entities_t *g_entities;
global_var struct voxel_chunks_t *g_voxel_chunks;


enum matrix4_mul_vec3_with_translation_flag { WITH_TRANSLATION, WITHOUT_TRANSLATION, TRANSLATION_DONT_CARE };

// ******************************** Voxel code ********************************

// Initialize rendering data, stuff that should only be initialize at the beginning of the game running in general
internal_function void hard_initialize_chunks(void)
{        
    g_voxel_chunks->chunk_model.attribute_count = 1;
    g_voxel_chunks->chunk_model.attributes_buffer = (VkVertexInputAttributeDescription *)allocate_free_list(sizeof(VkVertexInputAttributeDescription));
    g_voxel_chunks->chunk_model.binding_count = 1;
    g_voxel_chunks->chunk_model.bindings = (model_binding_t *)allocate_free_list(sizeof(model_binding_t));

    model_binding_t *binding = g_voxel_chunks->chunk_model.bindings;
    binding->begin_attributes_creation(g_voxel_chunks->chunk_model.attributes_buffer);

    // There is only one attribute for now
    binding->push_attribute(0, VK_FORMAT_R32G32B32_SFLOAT, sizeof(vector3_t));

    binding->end_attributes_creation();

    g_voxel_chunks->chunk_pipeline = g_pipeline_manager->add("pipeline.chunk_points"_hash);
    graphics_pipeline_t *voxel_pipeline = g_pipeline_manager->get(g_voxel_chunks->chunk_pipeline);
    {
        graphics_pipeline_info_t *info = (graphics_pipeline_info_t *)allocate_free_list(sizeof(graphics_pipeline_info_t));
        render_pass_handle_t dfr_render_pass = g_render_pass_manager->get_handle("render_pass.deferred_render_pass"_hash);
        shader_modules_t modules(shader_module_info_t{"shaders/SPV/voxel_point.vert.spv", VK_SHADER_STAGE_VERTEX_BIT},
                                 shader_module_info_t{"shaders/SPV/voxel_point.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT});
        shader_uniform_layouts_t layouts(g_uniform_layout_manager->get_handle("uniform_layout.camera_transforms_ubo"_hash));
        shader_pk_data_t push_k = {160, 0, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_GEOMETRY_BIT };
        shader_blend_states_t blending(false, false, false, false);
        dynamic_states_t dynamic(VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_LINE_WIDTH);
        fill_graphics_pipeline_info(modules, VK_FALSE, VK_PRIMITIVE_TOPOLOGY_POINT_LIST, VK_POLYGON_MODE_POINT,
                                    VK_CULL_MODE_NONE, layouts, push_k, get_backbuffer_resolution(), blending, &g_voxel_chunks->chunk_model,
                                    true, 0.0f, dynamic, g_render_pass_manager->get(dfr_render_pass), 0, info);
        voxel_pipeline->info = info;
        make_graphics_pipeline(voxel_pipeline);
    }

    g_voxel_chunks->chunk_mesh_pipeline = g_pipeline_manager->add("pipeline.chunk_mesh"_hash);
    graphics_pipeline_t *voxel_mesh_pipeline = g_pipeline_manager->get(g_voxel_chunks->chunk_mesh_pipeline);
    {
        graphics_pipeline_info_t *info = (graphics_pipeline_info_t *)allocate_free_list(sizeof(graphics_pipeline_info_t));
        render_pass_handle_t dfr_render_pass = g_render_pass_manager->get_handle("render_pass.deferred_render_pass"_hash);
        shader_modules_t modules(shader_module_info_t{"shaders/SPV/voxel_mesh.vert.spv", VK_SHADER_STAGE_VERTEX_BIT},
                                 shader_module_info_t{"shaders/SPV/voxel_mesh.geom.spv", VK_SHADER_STAGE_GEOMETRY_BIT},
                                 shader_module_info_t{"shaders/SPV/voxel_mesh.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT});
        shader_uniform_layouts_t layouts(g_uniform_layout_manager->get_handle("uniform_layout.camera_transforms_ubo"_hash),
                                         g_uniform_layout_manager->get_handle("descriptor_set_layout.2D_sampler_layout"_hash));
        shader_pk_data_t push_k = {160, 0, VK_SHADER_STAGE_VERTEX_BIT };
        shader_blend_states_t blending(false, false, false, false);
        dynamic_states_t dynamic(VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_LINE_WIDTH);
        fill_graphics_pipeline_info(modules, VK_FALSE, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_POLYGON_MODE_FILL,
                                    VK_CULL_MODE_NONE, layouts, push_k, get_backbuffer_resolution(), blending, &g_voxel_chunks->chunk_model,
                                    true, 0.0f, dynamic, g_render_pass_manager->get(dfr_render_pass), 0, info);
        voxel_mesh_pipeline->info = info;
        make_graphics_pipeline(voxel_mesh_pipeline);
    }

    g_voxel_chunks->chunk_mesh_shadow_pipeline = g_pipeline_manager->add("pipeline.chunk_mesh_shadow"_hash);
    graphics_pipeline_t *voxel_mesh_shadow_pipeline = g_pipeline_manager->get(g_voxel_chunks->chunk_mesh_shadow_pipeline);
    {
        graphics_pipeline_info_t *info = (graphics_pipeline_info_t *)allocate_free_list(sizeof(graphics_pipeline_info_t));
        auto shadow_display = get_shadow_display();
        VkExtent2D shadow_extent {shadow_display.shadowmap_w, shadow_display.shadowmap_h};
        render_pass_handle_t shadow_render_pass = g_render_pass_manager->get_handle("render_pass.shadow_render_pass"_hash);
        shader_modules_t modules(shader_module_info_t{"shaders/SPV/voxel_mesh_shadow.vert.spv", VK_SHADER_STAGE_VERTEX_BIT},
                                 shader_module_info_t{"shaders/SPV/voxel_mesh_shadow.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT});
        shader_uniform_layouts_t layouts(g_uniform_layout_manager->get_handle("uniform_layout.camera_transforms_ubo"_hash));
        shader_pk_data_t push_k = {160, 0, VK_SHADER_STAGE_VERTEX_BIT};
        shader_blend_states_t blending(false);
        dynamic_states_t dynamic(VK_DYNAMIC_STATE_DEPTH_BIAS, VK_DYNAMIC_STATE_VIEWPORT);
        fill_graphics_pipeline_info(modules, VK_FALSE, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_POLYGON_MODE_FILL,
                                    VK_CULL_MODE_NONE, layouts, push_k, shadow_extent, blending, &g_voxel_chunks->chunk_model,
                                    true, 0.0f, dynamic, g_render_pass_manager->get(shadow_render_pass), 0, info);
        voxel_mesh_shadow_pipeline->info = info;
        make_graphics_pipeline(voxel_mesh_shadow_pipeline);
    }

    g_voxel_chunks->gpu_queue = make_gpu_material_submission_queue(20 * 20 * 20, VK_SHADER_STAGE_VERTEX_BIT, VK_COMMAND_BUFFER_LEVEL_PRIMARY, get_global_command_pool());
}

internal_function vector3_t get_voxel_world_origin(void)
{
    return -vector3_t((float32_t)g_voxel_chunks->grid_edge_size / 2.0f) * (float32_t)(VOXEL_CHUNK_EDGE_LENGTH) * g_voxel_chunks->size;
}

// XS = voxel space (VS is view space)
internal_function vector3_t ws_to_xs(const vector3_t &ws_position)
{
    vector3_t from_origin = ws_position - get_voxel_world_origin();

    vector3_t xs_sized = from_origin / g_voxel_chunks->size;

    return xs_sized;
}

internal_function uint32_t convert_3d_to_1d_index(uint32_t x, uint32_t y, uint32_t z, uint32_t edge_length)
{
    return(z * (edge_length * edge_length) + y * edge_length + x);
}

// CS = chunk space
internal_function voxel_chunk_t *get_chunk_encompassing_point(const vector3_t &xs_position)
{
    ivector3_t rounded = ivector3_t(glm::round(xs_position));

    ivector3_t chunk_coord = rounded / (VOXEL_CHUNK_EDGE_LENGTH);

    return(*get_voxel_chunk(convert_3d_to_1d_index(chunk_coord.x, chunk_coord.y, chunk_coord.z, g_voxel_chunks->grid_edge_size)));
    
    ivector3_t cs_voxel_coord = ivector3_t(rounded.x % VOXEL_CHUNK_EDGE_LENGTH, rounded.y % (VOXEL_CHUNK_EDGE_LENGTH), rounded.z % (VOXEL_CHUNK_EDGE_LENGTH));
}

internal_function ivector3_t get_voxel_coord(const vector3_t &xs_position)
{
    ivector3_t rounded = ivector3_t(glm::round(xs_position));
    ivector3_t cs_voxel_coord = ivector3_t(rounded.x % VOXEL_CHUNK_EDGE_LENGTH, rounded.y % (VOXEL_CHUNK_EDGE_LENGTH), rounded.z % (VOXEL_CHUNK_EDGE_LENGTH));
    return(cs_voxel_coord);
}

internal_function void terraform(voxel_chunk_t *chunk, const ivector3_t &voxel_coord, uint32_t voxel_radius, bool destructive, float32_t dt)
{
    float32_t coefficient = (destructive ? -1.0f : 1.0f);
    
    float_t radius = (float32_t)voxel_radius;
    
    float32_t radius_squared = radius * radius;
    
    ivector3_t bottom_corner = voxel_coord - ivector3_t((uint32_t)radius);

    uint32_t diameter = (uint32_t)radius * 2 + 1;

    for (uint32_t z = 0; z < diameter; ++z)
    {
        for (uint32_t y = 0; y < diameter; ++y)
        {
            for (uint32_t x = 0; x < diameter; ++x)
            {
                vector3_t v_f = vector3_t(x, y, z) + vector3_t(bottom_corner);
                vector3_t diff = v_f - vector3_t(voxel_coord);
                float32_t real_distance_squared = glm::dot(diff, diff);
                if (real_distance_squared <= radius_squared)
                {
                    // Is in the sphere

                    // TODO: Handle case where voxel is in another chunk
                    uint8_t *voxel = &chunk->voxels[(uint32_t)v_f.x][(uint32_t)v_f.y][(uint32_t)v_f.z];
                    
                    int32_t current_voxel_value = (int32_t)*voxel;
                    // TODO: Change 150.0f to a speed parameter
                    int32_t new_value = (int32_t)(coefficient * dt * 250.0f) + current_voxel_value;

                    // TODO: Get rid of branching using floats (somehow)
                    if (new_value > 255)
                    {
                        *voxel = 255;
                    }
                    else if (new_value < 0)
                    {
                        *voxel = 0;
                    }
                    else
                    {
                        *voxel = (uint8_t)new_value;
                    }
                }
            }
        }
    }

    update_chunk_mesh(chunk, 60);
    ready_chunk_for_gpu_sync(chunk);
}

internal_function void construct_sphere(const vector3_t &ws_sphere_position, float32_t radius)
{
    vector3_t xs_sphere_position = ws_to_xs(ws_sphere_position);
    voxel_chunk_t *chunk = get_chunk_encompassing_point(xs_sphere_position);
    ivector3_t sphere_center = get_voxel_coord(xs_sphere_position);

    radius /= g_voxel_chunks->size;
    radius = glm::round(radius);
    
    float32_t radius_squared = radius * radius;
    
    ivector3_t bottom_corner = sphere_center - ivector3_t((uint32_t)radius);

    uint32_t diameter = (uint32_t)radius * 2 + 1;

    for (uint32_t z = 0; z < diameter; ++z)
    {
        for (uint32_t y = 0; y < diameter; ++y)
        {
            for (uint32_t x = 0; x < diameter; ++x)
            {
                vector3_t v_f = vector3_t(x, y, z) + vector3_t(bottom_corner);
                vector3_t diff = v_f - vector3_t(sphere_center);
                float32_t real_distance_squared = glm::dot(diff, diff);
                if (real_distance_squared <= radius_squared)
                {
                    // Is in the sphere

                    // TODO: Handle case where voxel is in another chunk
                    float32_t proportion = 1.0f - (real_distance_squared / radius_squared);
                    chunk->voxels[(uint32_t)v_f.x][(uint32_t)v_f.y][(uint32_t)v_f.z] = (uint32_t)((proportion) * 255.0f);
                    //chunk->voxels[(uint32_t)v_f.x][(uint32_t)v_f.y][(uint32_t)v_f.z] = 255;
                }
            }
        }
    }
}

internal_function void ray_cast_terraform(const vector3_t &ws_position, const vector3_t &ws_direction, float32_t max_reach_distance, float32_t dt, uint32_t surface_level, bool destructive)
{
    vector3_t ray_start_position = ws_to_xs(ws_position);
    vector3_t current_ray_position = ray_start_position;
    vector3_t ray_direction = ws_direction;
    max_reach_distance /= g_voxel_chunks->size;
    float32_t ray_step_size = max_reach_distance / 10.0f;
    float32_t max_reach_distance_squared = max_reach_distance * max_reach_distance;
    
    for (; glm::dot(current_ray_position - ray_start_position, current_ray_position - ray_start_position) < max_reach_distance_squared; current_ray_position += ray_step_size * ray_direction)
    {
        voxel_chunk_t *chunk = get_chunk_encompassing_point(current_ray_position);

        ivector3_t voxel_coord = get_voxel_coord(current_ray_position);

        if (chunk->voxels[voxel_coord.x][voxel_coord.y][voxel_coord.z] > surface_level)
        {
            terraform(chunk, voxel_coord, 2, destructive, dt);
            break;
        }
    }
}

void push_chunk_to_render_queue(voxel_chunk_t *chunk)
{
    g_voxel_chunks->gpu_queue.push_material(&chunk->push_k, sizeof(chunk->push_k), &chunk->gpu_mesh);
}

internal_function void initialize_chunk_vertices(voxel_chunk_t *chunk)
{
    uint32_t i = 0;
    constexpr uint32_t MAX_VERTS = MAX_VERTICES_PER_VOXEL_CHUNK;
    
    for (uint32_t z = 0; z < VOXEL_CHUNK_EDGE_LENGTH; ++z)
    {
        for (uint32_t y = 0; y < VOXEL_CHUNK_EDGE_LENGTH; ++y)
        {
            for (uint32_t x = 0; x < VOXEL_CHUNK_EDGE_LENGTH; ++x)
            {
                chunk->mesh_vertices[i++] = vector3_t(x, y, z);
            }
        }
    }

    chunk->vertex_count = i;
}

#include "ttable.inc"

internal_function inline float32_t lerp(float32_t a, float32_t b, float32_t x)
{
    return((x - a) / (b - a));
}

internal_function inline vector3_t interpolate(const vector3_t &a, const vector3_t &b, float32_t x)
{
    return(a + x * (b - a));
}

internal_function inline void push_vertex_to_triangle_array(uint8_t v0, uint8_t v1, vector3_t *vertices, voxel_chunk_t *chunk, uint8_t *voxel_values, uint8_t surface_level)
{
    float32_t surface_level_f = (float32_t)surface_level;
    float32_t voxel_value0 = (float32_t)voxel_values[v0];
    float32_t voxel_value1 = (float32_t)voxel_values[v1];

    if (voxel_value0 > voxel_value1)
    {
        float32_t tmp = voxel_value0;
        voxel_value0 = voxel_value1;
        voxel_value1 = tmp;

        uint8_t tmp_v = v0;
        v0 = v1;
        v1 = tmp_v;
    }

    float32_t interpolated_voxel_values = lerp(voxel_value0, voxel_value1, surface_level_f);
    
    vector3_t vertex = interpolate(vertices[v0], vertices[v1], interpolated_voxel_values);
    
    chunk->mesh_vertices[chunk->vertex_count++] = vertex;
}

internal_function void update_chunk_mesh_struct_vertex_count(voxel_chunk_t *chunk)
{
    chunk->gpu_mesh.indexed_data.index_count = chunk->vertex_count;
}

void update_chunk_mesh(voxel_chunk_t *chunk, uint8_t surface_level)
{
    chunk->vertex_count = 0;
    
    persist_var const vector3_t NORMALIZED_CUBE_VERTICES[8] = { vector3_t(-0.5f, -0.5f, -0.5f),
                                                                vector3_t(+0.5f, -0.5f, -0.5f),
                                                                vector3_t(+0.5f, -0.5f, +0.5f),
                                                                vector3_t(-0.5f, -0.5f, +0.5f),
                                                                vector3_t(-0.5f, +0.5f, -0.5f),
                                                                vector3_t(+0.5f, +0.5f, -0.5f),
                                                                vector3_t(+0.5f, +0.5f, +0.5f),
                                                                vector3_t(-0.5f, +0.5f, +0.5f) };

    persist_var const ivector3_t NORMALIZED_CUBE_VERTEX_INDICES[8] = { ivector3_t(0, 0, 0),
                                                                       ivector3_t(1, 0, 0),
                                                                       ivector3_t(1, 0, 1),
                                                                       ivector3_t(0, 0, 1),
                                                                       ivector3_t(0, 1, 0),
                                                                       ivector3_t(1, 1, 0),
                                                                       ivector3_t(1, 1, 1),
                                                                       ivector3_t(0, 1, 1) };
    
    for (uint32_t z = 0; z < VOXEL_CHUNK_EDGE_LENGTH - 1; ++z)
    {
        for (uint32_t y = 0; y < VOXEL_CHUNK_EDGE_LENGTH - 1; ++y)
        {
            for (uint32_t x = 0; x < VOXEL_CHUNK_EDGE_LENGTH - 1; ++x)
            {
                uint8_t voxel_values[8] = { chunk->voxels[x]    [y][z],
                                            chunk->voxels[x + 1][y][z],
                                            chunk->voxels[x + 1][y][z + 1],
                                            chunk->voxels[x]    [y][z + 1],
                     
                                            chunk->voxels[x]    [y + 1][z],
                                            chunk->voxels[x + 1][y + 1][z],
                                            chunk->voxels[x + 1][y + 1][z + 1],
                                            chunk->voxels[x]    [y + 1][z + 1] };

                uint8_t bit_combination = 0;
                for (uint32_t i = 0; i < 8; ++i)
                {
                    bool is_over_surface = (voxel_values[i] > surface_level);
                    bit_combination |= is_over_surface << i;
                }

                const int8_t *triangle_entry = &TRIANGLE_TABLE[bit_combination][0];

                uint32_t edge = 0;

                int8_t edge_pair[3] = {};
                
                while(triangle_entry[edge] != -1)
                {
                    int8_t edge_index = triangle_entry[edge];
                    edge_pair[edge % 3] = edge_index;

                    if (edge % 3 == 2)
                    {
                        vector3_t vertices[8] = {};
                        for (uint32_t i = 0; i < 8; ++i)
                        {
                            vertices[i] = NORMALIZED_CUBE_VERTICES[i] + vector3_t(0.5f) + vector3_t((float32_t)x, (float32_t)y, (float32_t)z);
                        }

                        uint8_t voxel_values[8] = {};

                        const ivector3_t *ncbi = NORMALIZED_CUBE_VERTEX_INDICES;
                        
                        for (uint32_t i = 0; i < 8; ++i)
                        {
                            voxel_values[i] = chunk->voxels [ x + ncbi[i].x ] [ y + ncbi[i].y ] [z + ncbi[i].z ];
                        }

                        for (uint32_t i = 0; i < 3; ++i)
                        {
                            switch(edge_pair[i])
                            {
                            case 0: { push_vertex_to_triangle_array(0, 1, vertices, chunk, voxel_values, surface_level); } break;
                            case 1: { push_vertex_to_triangle_array(1, 2, vertices, chunk, voxel_values, surface_level); } break;
                            case 2: { push_vertex_to_triangle_array(2, 3, vertices, chunk, voxel_values, surface_level); } break;
                            case 3: { push_vertex_to_triangle_array(3, 0, vertices, chunk, voxel_values, surface_level); } break;
                            case 4: { push_vertex_to_triangle_array(4, 5, vertices, chunk, voxel_values, surface_level); } break;
                            case 5: { push_vertex_to_triangle_array(5, 6, vertices, chunk, voxel_values, surface_level); } break;
                            case 6: { push_vertex_to_triangle_array(6, 7, vertices, chunk, voxel_values, surface_level); } break;
                            case 7: { push_vertex_to_triangle_array(7, 4, vertices, chunk, voxel_values, surface_level); } break;
                            case 8: { push_vertex_to_triangle_array(0, 4, vertices, chunk, voxel_values, surface_level); } break;
                            case 9: { push_vertex_to_triangle_array(1, 5, vertices, chunk, voxel_values, surface_level); } break;
                            case 10: { push_vertex_to_triangle_array(2, 6, vertices, chunk, voxel_values, surface_level); } break;
                            case 11: { push_vertex_to_triangle_array(3, 7, vertices, chunk, voxel_values, surface_level); } break;
                            }
                        }
                    }

                    ++edge;
                }
            }
        }
    }

    update_chunk_mesh_struct_vertex_count(chunk);
    ready_chunk_for_gpu_sync(chunk);
}

void ready_chunk_for_gpu_sync(voxel_chunk_t *chunk)
{
    // If it is already scheduled for GPU sync, don't push to the update stack
    if (!chunk->should_do_gpu_sync)
    {
        g_voxel_chunks->chunks_to_gpu_sync[g_voxel_chunks->to_sync_count++] = convert_3d_to_1d_index(chunk->chunk_coord.x, chunk->chunk_coord.y, chunk->chunk_coord.z, g_voxel_chunks->grid_edge_size);
        chunk->should_do_gpu_sync = 1;
    }
}

internal_function void sync_gpu_with_chunk_state(gpu_command_queue_t *queue)
{
    for (uint32_t i = 0; i < g_voxel_chunks->to_sync_count; ++i)
    {
        voxel_chunk_t *chunk = *get_voxel_chunk(g_voxel_chunks->chunks_to_gpu_sync[i]);

        update_gpu_buffer(&chunk->chunk_mesh_gpu_buffer,
                          chunk->mesh_vertices,
                          sizeof(vector3_t) * chunk->vertex_count,
                          0,
                          VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
                          VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT,
                          &queue->q);

        chunk->should_do_gpu_sync = 0;
    }

    g_voxel_chunks->to_sync_count = 0;
}

void initialize_chunk(voxel_chunk_t *chunk, vector3_t chunk_position, ivector3_t chunk_coord)
{
    chunk->chunk_coord = chunk_coord;
    
    memset(chunk->voxels, 0, sizeof(uint8_t) * VOXEL_CHUNK_EDGE_LENGTH * VOXEL_CHUNK_EDGE_LENGTH * VOXEL_CHUNK_EDGE_LENGTH);
    memset(chunk->mesh_vertices, 0, sizeof(vector3_t) * MAX_VERTICES_PER_VOXEL_CHUNK);
    
    uint32_t buffer_size = sizeof(vector3_t) * MAX_VERTICES_PER_VOXEL_CHUNK;

    make_unmappable_gpu_buffer(&chunk->chunk_mesh_gpu_buffer, buffer_size, chunk->mesh_vertices, gpu_buffer_usage_t::VERTEX_BUFFER, get_global_command_pool());

    draw_indexed_data_t indexed_data = init_draw_indexed_data_default(1, chunk->vertex_count);
    model_index_data_t model_indexed_data;
    memory_buffer_view_t<VkBuffer> buffers{1, &chunk->chunk_mesh_gpu_buffer.buffer};
    
    chunk->gpu_mesh = initialize_mesh(buffers, &indexed_data, &g_voxel_chunks->chunk_model.index_data);

    chunk->push_k.model_matrix = glm::scale(vector3_t(g_voxel_chunks->size)) * glm::translate(chunk_position);
    chunk->push_k.color = vector4_t(118.0 / 255.0, 169.0 / 255.0, 72.0 / 255.0, 1.0f);
}

voxel_chunk_t **get_voxel_chunk(uint32_t index)
{
    return(&g_voxel_chunks->chunks[index]);
}



// ********************* Entity code ***************************

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
    push_entity_to_queue(e, &g_entities->entity_mesh, &g_entities->entity_submission_queue);
}

internal_function void push_entity_to_rolling_queue(entity_t *e)
{
    rendering_component_t *component = &g_entities->rendering_components[ e->components.rendering_component ];

    uniform_group_t *group = nullptr;
    
    g_entities->rolling_entity_submission_queue.push_material(&component->push_k,
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
            component->push_k.ws_t = glm::translate(e->ws_p) * CORRECTION_90 * rot_matrix * e->rolling_rotation * glm::scale(e->size);
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

internal_function uint32_t add_terraform_power_component(entity_t *e)
{
    e->components.terraform_power_component = g_entities->terraform_power_component_count++;
    struct terraform_power_component_t *component = &g_entities->terraform_power_components[ e->components.terraform_power_component ];
    component->entity_index = e->index;
    
    return(e->components.terraform_power_component);
}

internal_function struct terraform_power_component_t *get_terraform_power_component(uint32_t index)
{
    return(&g_entities->terraform_power_components[index]);
}

internal_function void update_terraform_power_components(float32_t dt)
{
    for (uint32_t i = 0; i < g_entities->physics_component_count; ++i)
    {
        struct terraform_power_component_t *component = &g_entities->terraform_power_components[ i ];
        entity_t *e = &g_entities->entity_list[ component->entity_index ];
        uint32_t *action_flags = &e->action_flags;

        if (*action_flags & (1 << action_flags_t::ACTION_TERRAFORM_DESTROY))
        {
            ray_cast_terraform(e->ws_p, e->ws_d, 40.0f, dt, 60, 1);
        }

        if (*action_flags & (1 << action_flags_t::ACTION_TERRAFORM_ADD))
        {
            ray_cast_terraform(e->ws_p, e->ws_d, 40.0f, dt, 60, 0);
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
}

internal_function void update_rolling_entity_physics(struct physics_component_t *component, entity_t *e, uint32_t *action_flags, float32_t dt)
{
}

internal_function void update_not_physically_affected_entity(struct physics_component_t *component, entity_t *e, uint32_t *action_flags, float32_t dt)
{
    vector3_t result_force = vector3_t(0.0f);

    vector3_t right = glm::normalize(glm::cross(e->ws_d, vector3_t(0.0f, 1.0f, 0.0f)));
    vector3_t forward = glm::normalize(glm::cross(vector3_t(0.0f, 1.0f, 0.0f), right));

    if (*action_flags & (1 << action_flags_t::ACTION_FORWARD)) result_force += forward;
    if (*action_flags & (1 << action_flags_t::ACTION_BACK)) result_force -= forward;
    if (*action_flags & (1 << action_flags_t::ACTION_RIGHT)) result_force += right;
    if (*action_flags & (1 << action_flags_t::ACTION_LEFT)) result_force -= right;
    if (*action_flags & (1 << action_flags_t::ACTION_UP)) result_force += vector3_t(0.0f, 1.0f, 0.0f);
    if (*action_flags & (1 << action_flags_t::ACTION_DOWN)) result_force -= vector3_t(0.0f, 1.0f, 0.0f);

    result_force *= 100.0f;

    e->ws_p += result_force * dt;
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
        // Flying, not falling down towards earth
        else
        {
            update_not_physically_affected_entity(component, e, action_flags, dt);
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

        update_terraform_power_components(dt);
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
    physics->enabled = false;
    physics->hitbox.x_min = -1.001f;
    physics->hitbox.x_max = 1.001f;
    physics->hitbox.y_min = -1.001f;
    physics->hitbox.y_max = 1.001f;
    physics->hitbox.z_min = -1.001f;
    physics->hitbox.z_max = 1.001f;

    uint32_t terraform_power = add_terraform_power_component(r2_ptr);
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
        g_entities->entity_submission_queue.submit_queued_materials({1, &transforms_ubo_uniform_groups[image_index]}, model_ppln, queue, VK_COMMAND_BUFFER_LEVEL_PRIMARY);

        auto *rolling_model_ppln = g_pipeline_manager->get(g_entities->rolling_entity_shadow_ppln);
        g_entities->rolling_entity_submission_queue.submit_queued_materials({1, &transforms_ubo_uniform_groups[image_index]}, rolling_model_ppln, queue, VK_COMMAND_BUFFER_LEVEL_PRIMARY);

        g_voxel_chunks->gpu_queue.submit_queued_materials({1, &transforms_ubo_uniform_groups[image_index]}, g_pipeline_manager->get(g_voxel_chunks->chunk_mesh_shadow_pipeline), queue, VK_COMMAND_BUFFER_LEVEL_PRIMARY);
    }
    end_shadow_offscreen(queue);

    // Rendering the scene with lighting and everything
    begin_deferred_rendering(image_index, queue);
    {
        auto *entity_ppln = g_pipeline_manager->get(g_entities->entity_ppln);
        auto *rolling_entity_ppln = g_pipeline_manager->get(g_entities->rolling_entity_ppln);
    
        g_entities->entity_submission_queue.submit_queued_materials({2, uniform_groups}, entity_ppln, queue, VK_COMMAND_BUFFER_LEVEL_PRIMARY);
        g_entities->rolling_entity_submission_queue.submit_queued_materials({2, uniform_groups}, rolling_entity_ppln, queue, VK_COMMAND_BUFFER_LEVEL_PRIMARY);

        g_voxel_chunks->gpu_queue.submit_queued_materials({2, uniform_groups}, g_pipeline_manager->get(g_voxel_chunks->chunk_mesh_pipeline), queue, VK_COMMAND_BUFFER_LEVEL_PRIMARY);

        g_entities->entity_submission_queue.flush_queue();
        g_entities->rolling_entity_submission_queue.flush_queue();
        
        render_3d_frustum_debug_information(&uniform_groups[0], queue, image_index, g_pipeline_manager->get(g_entities->dbg_hitbox_ppln));
        
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

void hard_initialize_world(input_state_t *input_state, VkCommandPool *cmdpool, application_type_t app_type, application_mode_t app_mode)
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
        g_entities->rolling_entity_submission_queue = make_gpu_material_submission_queue(10, VK_SHADER_STAGE_VERTEX_BIT, VK_COMMAND_BUFFER_LEVEL_PRIMARY, cmdpool);
        g_entities->entity_submission_queue = make_gpu_material_submission_queue(20, VK_SHADER_STAGE_VERTEX_BIT, VK_COMMAND_BUFFER_LEVEL_PRIMARY, cmdpool);
    }

    if (app_type == application_type_t::WINDOW_APPLICATION_MODE)
    {
        initialize_entities_graphics_data(cmdpool, input_state);
    }

    hard_initialize_chunks();
    
    initialize_world(input_state, cmdpool, app_type, app_mode);
    
    clear_linear();
}

void initialize_world(input_state_t *input_state, VkCommandPool *cmdpool, application_type_t app_type, application_mode_t app_mode)
{
    g_voxel_chunks->size = 10.0f;
    g_voxel_chunks->grid_edge_size = 1;
    
    initialize_entities_data(cmdpool, input_state, app_type);
    
    g_voxel_chunks->max_chunks = 20 * 20 * 20;
    g_voxel_chunks->chunks = (voxel_chunk_t **)allocate_free_list(sizeof(voxel_chunk_t *) * g_voxel_chunks->max_chunks);
    memset(g_voxel_chunks->chunks, 0, sizeof(voxel_chunk_t *) * g_voxel_chunks->max_chunks);

    uint32_t i = 0;
    
    for (uint32_t z = 0; z < g_voxel_chunks->grid_edge_size; ++z)
    {
        for (uint32_t y = 0; y < g_voxel_chunks->grid_edge_size; ++y)
        {
            for (uint32_t x = 0; x < g_voxel_chunks->grid_edge_size; ++x)
            {
                voxel_chunk_t **chunk_ptr = get_voxel_chunk(i);
                *chunk_ptr = (voxel_chunk_t *)allocate_free_list(sizeof(voxel_chunk_t));
    
                initialize_chunk(*chunk_ptr, vector3_t(x, y, z) * (float32_t)(VOXEL_CHUNK_EDGE_LENGTH) - vector3_t((float32_t)g_voxel_chunks->grid_edge_size / 2) * (float32_t)(VOXEL_CHUNK_EDGE_LENGTH), ivector3_t(x, y, z));
                push_chunk_to_render_queue(*chunk_ptr);

                ++i;
            }    
        }    
    }

    construct_sphere(vector3_t(0.0f), 60.0f);
    update_chunk_mesh(*get_voxel_chunk(0), 60);
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

    g_entities->rolling_entity_submission_queue.mtrl_count = 0;
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
}

void sync_gpu_memory_with_world_state(gpu_command_queue_t *cmdbuf, uint32_t image_index)
{
    update_animation_gpu_data(cmdbuf);
    update_3d_output_camera_transforms(image_index);

    sync_gpu_with_chunk_state(cmdbuf);
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
        }
        else
        {
        }
    }
}

void handle_main_entity_mouse_button_input(entity_t *e, uint32_t *action_flags, input_state_t *input_state, float32_t dt)
{
    if (input_state->mouse_buttons[mouse_button_type_t::MOUSE_RIGHT].is_down)
    {
        *action_flags |= (1 << action_flags_t::ACTION_TERRAFORM_ADD);
    }

    if (input_state->mouse_buttons[mouse_button_type_t::MOUSE_LEFT].is_down)
    {
        *action_flags |= (1 << action_flags_t::ACTION_TERRAFORM_DESTROY);
    }
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
        *action_flags |= (1 << action_flags_t::ACTION_UP);
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

        handle_main_entity_keyboard_input(e, &e->action_flags, e_physics, input_state, dt);
        handle_main_entity_mouse_movement(e, &e->action_flags, input_state, dt);
        handle_main_entity_mouse_button_input(e, &e->action_flags, input_state, dt);
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
    g_voxel_chunks = &memory->world_state.voxel_chunks;
}

internal_function int32_t lua_reinitialize(lua_State *state)
{
    return(0);
}

