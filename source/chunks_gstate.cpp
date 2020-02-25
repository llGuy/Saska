#include "entities_gstate.hpp"
#include "chunks_gstate.hpp"
#include "graphics.hpp"
#include "packets.hpp"
#include "script.hpp"
#include "chunk.hpp"
#include "game.hpp"
#include "net.hpp"
#include "map.hpp"

#include "deferred_renderer.hpp"

constexpr uint8_t VOXEL_HAS_NOT_BEEN_APPENDED_TO_HISTORY = 255;
constexpr float32_t MAX_VOXEL_VALUE = 254.0f;
constexpr uint32_t MAX_VOXEL_COLOR_BEACON_COUNT = 50;


struct alignas(16) voxel_color_beacon_t {
    vector4_t ws_position;
    vector4_t color;
    float32_t reach; // Radius
    float32_t roughness;
    float32_t metalness;
    
    float32_t power;
};


// Global
static uint8_t dummy_voxels[CHUNK_EDGE_LENGTH][CHUNK_EDGE_LENGTH][CHUNK_EDGE_LENGTH];
static uint32_t grid_edge_size;
static float32_t chunk_size;
static uint32_t chunk_count;
static uint32_t max_chunks;
static chunk_t **chunks;
static model_t chunk_model;
static pipeline_handle_t chunk_mesh_pipeline, chunk_mesh_shadow_pipeline;
static gpu_material_submission_queue_t gpu_queue;
static uint32_t chunks_to_render_count = 0;
static chunk_t **chunks_to_update;
static uint32_t to_sync_count = 0;
static uint32_t chunks_to_gpu_sync[20];

static alignas(16) struct {

    voxel_color_beacon_t default_voxel_color;
    voxel_color_beacon_t voxel_color_beacons[MAX_VOXEL_COLOR_BEACON_COUNT] = {};
    int32_t beacon_count;
    
} voxel_beacons;

static gpu_buffer_t voxel_color_beacon_uniform_buffer;
static uniform_layout_handle_t voxel_color_beacon_ulayout;
static uniform_group_t voxel_color_beacon_uniform;

static constexpr uint32_t MAX_MODIFIED_CHUNKS = 32;
static uint32_t modified_chunks_count = 0;
static chunk_t *modified_chunks[MAX_MODIFIED_CHUNKS] = {};
static float32_t elapsed_interpolation_time = 0.0f;

static chunks_state_flags_t flags;

static linear_allocator_t voxel_linear_allocator_front = {};
static game_snapshot_voxel_delta_packet_t *previous_voxel_delta_packet_front = nullptr;



// Static declarations
static void s_construct_plane(const vector3_t &ws_plane_origin, float32_t radius);
static void s_construct_sphere(const vector3_t &ws_sphere_position, float32_t radius);
static void s_clear_voxels();
static void s_terraform_with_history(const ivector3_t &xs_voxel_coord, uint32_t voxel_radius, bool destructive, float32_t dt, float32_t speed);
static void s_append_chunk_to_history_of_modified_chunks_if_not_already(chunk_t *chunk);
static void s_flag_chunks_previously_modified_by_client(client_t *user_client);
static void s_unflag_chunks_previously_modified_by_client(client_t *user_client);
static void s_fill_dummy_voxels(client_modified_chunk_nl_t *chunk);
static void s_unfill_dummy_voxels(client_modified_chunk_nl_t *chunk);


static int32_t s_lua_clear_voxels(lua_State *state);
static int32_t s_lua_create_sphere(lua_State *state);
static int32_t s_lua_save_map(lua_State *state);

// "Public" definitions
void initialize_chunks_state(void) {
    add_global_to_lua(script_primitive_type_t::FUNCTION, "clear_voxels", &s_lua_clear_voxels);
    add_global_to_lua(script_primitive_type_t::FUNCTION, "create_sphere", &s_lua_create_sphere);
    add_global_to_lua(script_primitive_type_t::FUNCTION, "save_map", &s_lua_save_map);
    
    switch(get_app_type()) {
    case application_type_t::WINDOW_APPLICATION_MODE: {
        voxel_color_beacon_ulayout = g_uniform_layout_manager->add("uniform_layout.voxel_color_beacon"_hash);
        auto *layout_ptr = g_uniform_layout_manager->get(voxel_color_beacon_ulayout);

        uniform_layout_info_t voxel_color_beacon_ulayout_blueprint = {};
        voxel_color_beacon_ulayout_blueprint.push(1, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_GEOMETRY_BIT);
        *layout_ptr = make_uniform_layout(&voxel_color_beacon_ulayout_blueprint);

        voxel_color_beacon_uniform = make_uniform_group(layout_ptr, g_uniform_pool);

        chunk_model.attribute_count = 1;
        chunk_model.attributes_buffer = (VkVertexInputAttributeDescription *)allocate_free_list(sizeof(VkVertexInputAttributeDescription));
        chunk_model.binding_count = 1;
        chunk_model.bindings = (model_binding_t *)allocate_free_list(sizeof(model_binding_t));

        model_binding_t *binding = chunk_model.bindings;
        binding->begin_attributes_creation(chunk_model.attributes_buffer);

        // There is only one attribute for now
        binding->push_attribute(0, VK_FORMAT_R32G32B32_SFLOAT, sizeof(vector3_t));

        binding->end_attributes_creation();

        chunk_mesh_pipeline = g_pipeline_manager->add("pipeline.chunk_mesh"_hash);
        graphics_pipeline_t *voxel_mesh_pipeline = g_pipeline_manager->get(chunk_mesh_pipeline);
        {
            graphics_pipeline_info_t *info = (graphics_pipeline_info_t *)allocate_free_list(sizeof(graphics_pipeline_info_t));
            render_pass_handle_t dfr_render_pass = g_render_pass_manager->get_handle("render_pass.deferred_render_pass"_hash);
            shader_modules_t modules(shader_module_info_t{ "shaders/SPV/voxel_mesh.vert.spv", VK_SHADER_STAGE_VERTEX_BIT },
                shader_module_info_t{ "shaders/SPV/voxel_mesh.geom.spv", VK_SHADER_STAGE_GEOMETRY_BIT },
                shader_module_info_t{ "shaders/SPV/voxel_mesh.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT });
            shader_uniform_layouts_t layouts(g_uniform_layout_manager->get_handle("uniform_layout.camera_transforms_ubo"_hash),
                g_uniform_layout_manager->get_handle("descriptor_set_layout.2D_sampler_layout"_hash),
                voxel_color_beacon_ulayout);
            shader_pk_data_t push_k = { 160, 0, VK_SHADER_STAGE_VERTEX_BIT };
            shader_blend_states_t blending(blend_type_t::NO_BLENDING, blend_type_t::NO_BLENDING, blend_type_t::NO_BLENDING, blend_type_t::NO_BLENDING, blend_type_t::NO_BLENDING);
            dynamic_states_t dynamic(VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_LINE_WIDTH);
            fill_graphics_pipeline_info(modules, VK_FALSE, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_POLYGON_MODE_FILL,
                VK_CULL_MODE_NONE, layouts, push_k, backbuffer_resolution(), blending, &chunk_model,
                true, 0.0f, dynamic, g_render_pass_manager->get(dfr_render_pass), 0, info);
            voxel_mesh_pipeline->info = info;
            make_graphics_pipeline(voxel_mesh_pipeline);
        }

        chunk_mesh_shadow_pipeline = g_pipeline_manager->add("pipeline.chunk_mesh_shadow"_hash);
        graphics_pipeline_t *voxel_mesh_shadow_pipeline = g_pipeline_manager->get(chunk_mesh_shadow_pipeline);
        {
            graphics_pipeline_info_t *info = (graphics_pipeline_info_t *)allocate_free_list(sizeof(graphics_pipeline_info_t));
            auto shadow_display = get_shadow_display();
            VkExtent2D shadow_extent{ shadow_display.shadowmap_w, shadow_display.shadowmap_h };
            render_pass_handle_t shadow_render_pass = g_render_pass_manager->get_handle("render_pass.shadow_render_pass"_hash);
            shader_modules_t modules(shader_module_info_t{ "shaders/SPV/voxel_mesh_shadow.vert.spv", VK_SHADER_STAGE_VERTEX_BIT },
                                     shader_module_info_t{ "shaders/SPV/voxel_mesh_shadow.geom.spv", VK_SHADER_STAGE_GEOMETRY_BIT },
                                     shader_module_info_t{ "shaders/SPV/voxel_mesh_shadow.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT });
            shader_uniform_layouts_t layouts(g_uniform_layout_manager->get_handle("uniform_layout.camera_transforms_ubo"_hash));
            shader_pk_data_t push_k = { 240, 0, VK_SHADER_STAGE_VERTEX_BIT };
            shader_blend_states_t blending(blend_type_t::NO_BLENDING);
            dynamic_states_t dynamic(VK_DYNAMIC_STATE_DEPTH_BIAS, VK_DYNAMIC_STATE_VIEWPORT);
            fill_graphics_pipeline_info(modules, VK_FALSE, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_POLYGON_MODE_FILL,
                VK_CULL_MODE_NONE, layouts, push_k, shadow_extent, blending, &chunk_model,
                true, 0.0f, dynamic, g_render_pass_manager->get(shadow_render_pass), 0, info);
            voxel_mesh_shadow_pipeline->info = info;
            make_graphics_pipeline(voxel_mesh_shadow_pipeline);
        }
    } break;

    default: break;
    }

    gpu_queue = make_gpu_material_submission_queue(20 * 20 * 20, VK_SHADER_STAGE_VERTEX_BIT, VK_COMMAND_BUFFER_LEVEL_PRIMARY, get_global_command_pool());

    map_data_t map_data;
    load_map(&map_data, "maps/sandbox.map", &chunk_model);

    chunk_size = map_data.chunk_size;
    grid_edge_size = map_data.grid_edge_size;
    max_chunks = 20 * 20 * 20;
    chunks = map_data.chunks;
    chunks_to_update = map_data.to_update;
    chunks_to_render_count = map_data.to_update_count;

/*    chunk_size = 9.0f;
    grid_edge_size = 5;

    max_chunks = 20 * 20 * 20;
    chunks = (chunk_t **)allocate_free_list(sizeof(chunk_t *) * max_chunks);
    memset(chunks, 0, sizeof(chunk_t *) * max_chunks);
    chunks_to_render_count = 0;
    chunks_to_update = (chunk_t **)allocate_free_list(sizeof(chunk_t *) * max_chunks);

    uint32_t i = 0;
    
    for (uint32_t z = 0; z < grid_edge_size; ++z) {
        for (uint32_t y = 0; y < grid_edge_size; ++y) {
            for (uint32_t x = 0; x < grid_edge_size; ++x) {
                chunk_t **chunk_pptr = get_chunk((int32_t)i);
                *chunk_pptr = (chunk_t *)allocate_free_list(sizeof(chunk_t));
    
                vector3_t position = vector3_t(x, y, z) * (float32_t)(CHUNK_EDGE_LENGTH) - vector3_t((float32_t)grid_edge_size / 2) * (float32_t)(CHUNK_EDGE_LENGTH);

                chunk_t *chunk_ptr = *chunk_pptr;
                chunk_ptr->initialize(position, ivector3_t(x, y, z), true, vector3_t(chunk_size));

                switch (get_app_type()) {
                case application_type_t::WINDOW_APPLICATION_MODE: {
                    chunk_ptr->initialize_for_rendering(&chunk_model);
                } break;
                default: break;
                }

                ++i;
            }    
        }    
    }

    s_construct_sphere(vector3_t(-20.0f, 70.0f, -120.0f), 60.0f);
    s_construct_sphere(vector3_t(-80.0f, -50.0f, 0.0f), 70.0f);
    s_construct_sphere(vector3_t(80.0f, 50.0f, 0.0f), 70.0f);
    s_construct_plane(vector3_t(0.0f, -100.0f, 0.0f), 60.0f);*/


    voxel_beacons.default_voxel_color.color = vector4_t(52.0f, 150.0f, 2.0f, 255.0f) / 255.0f;
    voxel_beacons.default_voxel_color.metalness = 0.15f;
    voxel_beacons.default_voxel_color.roughness = 0.7f;

    voxel_beacons.beacon_count = 0;

    switch (get_app_type()) {
    case application_type_t::WINDOW_APPLICATION_MODE: {
        // Initialize voxel color beacons - will update this uniform buffer every time game gets initialized
        make_unmappable_gpu_buffer(&voxel_color_beacon_uniform_buffer, sizeof(voxel_beacons), &voxel_beacons, gpu_buffer_usage_t::UNIFORM_BUFFER, get_global_command_pool());
        update_uniform_group(&voxel_color_beacon_uniform, update_binding_t{ BUFFER, &voxel_color_beacon_uniform_buffer, 0 });
        VkCommandBuffer cmdbuf;
        init_single_use_command_buffer(get_global_command_pool(), &cmdbuf);
        update_gpu_buffer(&voxel_color_beacon_uniform_buffer, &voxel_beacons, sizeof(voxel_beacons), 0, VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT, &cmdbuf);
        destroy_single_use_command_buffer(&cmdbuf, get_global_command_pool());
    } break;
    default: break;
    }
    
    voxel_linear_allocator_front.current = voxel_linear_allocator_front.start = allocate_free_list(sizeof(uint8_t) * 5000);
    voxel_linear_allocator_front.capacity = sizeof(uint8_t) * 5000;

    memset(dummy_voxels, 255, sizeof(uint8_t) * CHUNK_EDGE_LENGTH * CHUNK_EDGE_LENGTH * CHUNK_EDGE_LENGTH);
    previous_voxel_delta_packet_front = (game_snapshot_voxel_delta_packet_t *)allocate_free_list(sizeof(game_snapshot_voxel_delta_packet_t));

    get_chunks_state_flags()->should_update_chunk_meshes_from_now = 1;
}


static bool editor_mode = 0;


void start_map_editor_mode() {
    editor_mode = 1;
}


void stop_map_editor_mode() {
    editor_mode = 0;
}


void populate_chunks_state(game_state_initialize_packet_t *packet) {
    if (packet) {
        chunk_size = packet->voxels.size;
        grid_edge_size = packet->voxels.grid_edge_size;

        chunks_to_render_count = 0;
        chunks_to_update = (chunk_t * *)allocate_free_list(sizeof(chunk_t *) * grid_edge_size * grid_edge_size * grid_edge_size);

        max_chunks = packet->voxels.max_chunks;
        chunks = (chunk_t * *)allocate_free_list(sizeof(chunk_t *) * max_chunks);
        memset(chunks, 0, sizeof(chunk_t *) * max_chunks);

        uint32_t i = 0;

        for (uint32_t z = 0; z < grid_edge_size; ++z) {
            for (uint32_t y = 0; y < grid_edge_size; ++y) {
                for (uint32_t x = 0; x < grid_edge_size; ++x) {
                    chunk_t **chunk_pptr = get_chunk((int32_t)i);
                    *chunk_pptr = (chunk_t *)allocate_free_list(sizeof(chunk_t));

                    vector3_t position = vector3_t(x, y, z) * (float32_t)(CHUNK_EDGE_LENGTH)-vector3_t((float32_t)grid_edge_size / 2) * (float32_t)(CHUNK_EDGE_LENGTH);

                    chunk_t *chunk_ptr = *chunk_pptr;
                    chunk_ptr->initialize(position, ivector3_t(x, y, z), true, vector3_t(chunk_size));

                    switch (get_app_type()) {
                    case application_type_t::WINDOW_APPLICATION_MODE: {
                        chunk_ptr->initialize_for_rendering(&chunk_model);
                    } break;
                    default: break;
                    }

                    ++i;
                }
            }
        }
    }
    else {
        chunk_size = 9;
        grid_edge_size = 5;

        chunks_to_render_count = 0;
        chunks_to_update = (chunk_t * *)allocate_free_list(sizeof(chunk_t *) * grid_edge_size * grid_edge_size * grid_edge_size);

        max_chunks = 32;
        chunks = (chunk_t * *)allocate_free_list(sizeof(chunk_t *) * max_chunks);
        memset(chunks, 0, sizeof(chunk_t *) * max_chunks);

        uint32_t i = 0;

        for (uint32_t z = 0; z < grid_edge_size; ++z) {
            for (uint32_t y = 0; y < grid_edge_size; ++y) {
                for (uint32_t x = 0; x < grid_edge_size; ++x) {
                    chunk_t **chunk_pptr = get_chunk((int32_t)i);
                    *chunk_pptr = (chunk_t *)allocate_free_list(sizeof(chunk_t));

                    vector3_t position = vector3_t(x, y, z) * (float32_t)(CHUNK_EDGE_LENGTH)-vector3_t((float32_t)grid_edge_size / 2) * (float32_t)(CHUNK_EDGE_LENGTH);

                    chunk_t *chunk_ptr = *chunk_pptr;
                    chunk_ptr->initialize(position, ivector3_t(x, y, z), true, vector3_t(chunk_size));

                    switch (get_app_type()) {
                    case application_type_t::WINDOW_APPLICATION_MODE: {
                        chunk_ptr->initialize_for_rendering(&chunk_model);
                    } break;
                    default: break;

                        ++i;
                    }
                }
            }
        }
    }
}


void populate_chunks_state(const char *map_path) {
    map_data_t map_data;
    load_map(&map_data, map_path, &chunk_model);

    chunk_size = map_data.chunk_size;
    grid_edge_size = map_data.grid_edge_size;
    max_chunks = 20 * 20 * 20;
    chunks = map_data.chunks;
    chunks_to_update = map_data.to_update;
    chunks_to_render_count = map_data.to_update_count;
}


void deinitialize_chunks_state(void) {
    /*map_data_t data = {};
    data.grid_edge_size = grid_edge_size;
    data.chunk_size = chunk_size;
    data.to_update_count = chunks_to_render_count;
    data.chunks = chunks;
    data.to_update = chunks_to_update;
    save_map(&data, "maps/sandbox.map");*/

    uint32_t i = 0;
    for (uint32_t z = 0; z < grid_edge_size; ++z) {
        for (uint32_t y = 0; y < grid_edge_size; ++y) {
            for (uint32_t x = 0; x < grid_edge_size; ++x) {
                chunk_t **chunk_pptr = get_chunk((int32_t)i);
                chunk_t *chunk_ptr = *chunk_pptr;
                chunk_ptr->deinitialize();
                
                deallocate_free_list(chunk_ptr);
    
                ++i;
            }    
        }    
    }
    
    deallocate_free_list(chunks);
    deallocate_free_list(chunks_to_update);

    grid_edge_size = 0;
    chunk_size = 0;
    chunk_count = 0;
    max_chunks = 0;
    to_sync_count = 0;
}


void fill_game_state_initialize_packet_with_chunk_state(struct game_state_initialize_packet_t *packet) {
    packet->voxels.size = chunk_size;
    packet->voxels.grid_edge_size = grid_edge_size;
    packet->voxels.max_chunks = max_chunks;
    packet->voxels.chunk_count = chunk_count;
}


voxel_chunk_values_packet_t *initialize_chunk_values_packets(uint32_t *count) {
    voxel_chunk_values_packet_t *packets = (voxel_chunk_values_packet_t *)allocate_linear(sizeof(voxel_chunk_values_packet_t) * chunks_to_render_count);
    for (uint32_t i = 0; i < chunks_to_render_count; ++i) {
        chunk_t *chunk = chunks_to_update[i];
        packets[i].chunk_coord_x = chunk->chunk_coord.x;
        packets[i].chunk_coord_y = chunk->chunk_coord.y;
        packets[i].chunk_coord_z = chunk->chunk_coord.z;
        packets[i].voxels = &chunk->voxels[0][0][0];
    }

    *count = chunks_to_render_count;

    return(packets);
}


void ray_cast_terraform(const vector3_t &ws_position, const vector3_t &ws_direction, float32_t max_reach_distance, float32_t dt, uint32_t surface_level, bool destructive, float32_t speed) {
    vector3_t ray_start_position = ws_to_xs(ws_position);
    vector3_t current_ray_position = ray_start_position;
    vector3_t ray_direction = ws_direction;
    max_reach_distance /= chunk_size;
    float32_t ray_step_size = max_reach_distance / 10.0f;
    float32_t max_reach_distance_squared = max_reach_distance * max_reach_distance;
    
    for (; glm::dot(current_ray_position - ray_start_position, current_ray_position - ray_start_position) < max_reach_distance_squared; current_ray_position += ray_step_size * ray_direction) {
        chunk_t *chunk = get_chunk_encompassing_point(current_ray_position);

        if (chunk) {
            ivector3_t voxel_coord = get_voxel_coord(current_ray_position);

            if (chunk->voxels[voxel_coord.x][voxel_coord.y][voxel_coord.z] > surface_level) {
                s_terraform_with_history(ivector3_t(current_ray_position), 2, destructive, dt, speed);
                
                break;
            }
        }
    }
}


void tick_chunks_state(float32_t dt) {
    gpu_queue.flush_queue();
    chunks_to_render_count = 0;

    // This is the maximum interpolation time
    float32_t server_snapshot_rate = get_snapshot_server_rate();

    bool did_correction = 0;
    
    if (elapsed_interpolation_time < server_snapshot_rate - dt && previous_voxel_delta_packet_front) {
        elapsed_interpolation_time += dt;
        float32_t progression = elapsed_interpolation_time / server_snapshot_rate;

        if (progression > 1.0f) {
            progression = 1.0f;
        }
        
        player_t *user = get_user_player();
        client_t *user_client = get_user_client();

        // Flag chunks that have been modified by the client
        s_flag_chunks_previously_modified_by_client(user_client);
        {
            game_snapshot_voxel_delta_packet_t *voxel_delta = get_previous_voxel_delta_packet();

            // Loop through the chunks seen as modified by the server (will probably also be chunks modified by other clients)
            for (uint32_t modified_chunk_index = 0; modified_chunk_index < voxel_delta->modified_count; ++modified_chunk_index) {
                chunk_t *modified_chunk_ptr = *get_chunk(voxel_delta->modified_chunks[modified_chunk_index].chunk_index);
            
                // Check if chunk was modified previously by this client (by checking the flags we just modified - or not modified)
                if (modified_chunk_ptr->was_previously_modified_by_client) {
                    // If was modified, fill dummy voxels with the modified voxels of the client
                    client_modified_chunk_nl_t *local_chunk_modifications = &user_client->previous_received_voxel_modifications[modified_chunk_ptr->index_of_modified_chunk /* Was filled in by flag_chunks_modified_by_client() */];
                
                    // Fill dummy voxels (temporarily)
                    s_fill_dummy_voxels(local_chunk_modifications);
                    {
                        for (uint32_t sm_voxel = 0; sm_voxel < voxel_delta->modified_chunks[modified_chunk_index].modified_voxel_count; ++sm_voxel) {
                            modified_voxel_t *sm_voxel_ptr = &voxel_delta->modified_chunks[modified_chunk_index].modified_voxels[sm_voxel];

                            voxel_coordinate_t coord = convert_1d_to_3d_coord(sm_voxel_ptr->index, CHUNK_EDGE_LENGTH);
                            if (dummy_voxels[coord.x][coord.y][coord.z] != 255) {
                                // Voxel has been modified
                                if (user_client->needs_to_do_voxel_correction) {
                                    if (dummy_voxels[coord.x][coord.y][coord.z] != sm_voxel_ptr->next_value) {
                                        // This happens elsewhere now
                                    }
                                }
                            }
                            else {
                                modified_voxel_t *voxel_ptr = sm_voxel_ptr;
                                float32_t interpolated_value_f = interpolate((float32_t)voxel_ptr->previous_value, (float32_t)voxel_ptr->next_value, progression);
                                //uint8_t interpolated_value = (uint8_t)interpolated_value_f;
                                voxel_coordinate_t coord = convert_1d_to_3d_coord(voxel_ptr->index, CHUNK_EDGE_LENGTH);
                                //modified_chunk_ptr->voxels[coord.x][coord.y][coord.z] = interpolated_value;
                                modified_chunk_ptr->voxels[coord.x][coord.y][coord.z] = voxel_ptr->next_value;
                            }
                        }
                    }
                    // Unfill dummy voxels
                    s_unfill_dummy_voxels(local_chunk_modifications);
                }
                else {
                    // Just interpolate voxel values (simple)
                    for (uint32_t sm_voxel = 0; sm_voxel < voxel_delta->modified_chunks[modified_chunk_index].modified_voxel_count; ++sm_voxel) {
                        modified_voxel_t *voxel_ptr = &voxel_delta->modified_chunks[modified_chunk_index].modified_voxels[sm_voxel];
                        float32_t interpolated_value_f = interpolate((float32_t)voxel_ptr->previous_value, (float32_t)voxel_ptr->next_value, progression);
                        //uint8_t interpolated_value = (uint8_t)interpolated_value_f;
                        voxel_coordinate_t coord = convert_1d_to_3d_coord(voxel_ptr->index, CHUNK_EDGE_LENGTH);
                        //modified_chunk_ptr->voxels[coord.x][coord.y][coord.z] = interpolated_value;
                        modified_chunk_ptr->voxels[coord.x][coord.y][coord.z] = voxel_ptr->next_value;
                    }
                }

                ready_chunk_for_gpu_sync(modified_chunk_ptr);
            }
        }
        // Unflag chunks that have been modified by the client
        s_unflag_chunks_previously_modified_by_client(user_client);
    }
    else {
        //previous_voxel_delta_packet = nullptr;
    };
}


void render_chunks_to_shadowmap(uniform_group_t *transforms_ubo_uniform_groups, gpu_command_queue_t *queue) {
    gpu_queue.submit_queued_materials({1, transforms_ubo_uniform_groups}, g_pipeline_manager->get(chunk_mesh_shadow_pipeline), queue, VK_COMMAND_BUFFER_LEVEL_PRIMARY);
}


void render_chunks(uniform_group_t *uniforms, gpu_command_queue_t *queue) {
    uniform_group_t groups[3] = { uniforms[0], uniforms[1], voxel_color_beacon_uniform };
    gpu_queue.submit_queued_materials({3, groups}, g_pipeline_manager->get(chunk_mesh_pipeline), queue, VK_COMMAND_BUFFER_LEVEL_PRIMARY);
}


void sync_gpu_with_chunks_state(gpu_command_queue_t *queue) {
    // TODO: Find way to avoid this loop
    // Push all chunks with active vertices
    for (uint32_t z = 0; z < grid_edge_size; ++z) {
        for (uint32_t y = 0; y < grid_edge_size; ++y) {
            for (uint32_t x = 0; x < grid_edge_size; ++x) {
                chunk_t *chunk = *get_chunk(x, y, z);
                if (chunk->vertex_count) {
                    gpu_queue.push_material(&chunk->push_k, sizeof(chunk->push_k), &chunk->gpu_mesh);
                    chunks_to_update[chunks_to_render_count++] = chunk;
                }
            }
        }
    }

    
    // Update vertex buffers of chunks that just had their vertices updated
    if (flags.should_update_chunk_meshes_from_now) {
        for (uint32_t i = 0; i < to_sync_count; ++i) {
            chunk_t *chunk = *get_chunk((int32_t)chunks_to_gpu_sync[i]);
            
            chunk->update_mesh(60, queue);
        }

        to_sync_count = 0;
    }
}


void reset_voxel_interpolation(void) {
    // Run interpolating one more time, except this time, force set all the "next values"
    bool did_correction = 0;
    
    if (previous_voxel_delta_packet_front) {
        elapsed_interpolation_time = 0.0f;
        
        player_t *user = get_user_player();
        client_t *user_client = get_user_client();

        // Flag chunks that have been modified by the client
        s_flag_chunks_previously_modified_by_client(user_client);
        {
            game_snapshot_voxel_delta_packet_t *voxel_delta = get_previous_voxel_delta_packet();
        
            // Loop through the chunks seen as modified by the server (will probably also be chunks modified by other clients)
            for (uint32_t modified_chunk_index = 0; modified_chunk_index < voxel_delta->modified_count; ++modified_chunk_index) {
                chunk_t *modified_chunk_ptr = *get_chunk(voxel_delta->modified_chunks[modified_chunk_index].chunk_index);
            
                // Check if chunk was modified previously by this client (by checking the flags we just modified - or not modified)
                if (modified_chunk_ptr->was_previously_modified_by_client) {
                    // If was modified, fill dummy voxels with the modified voxels of the client
                    client_modified_chunk_nl_t *local_chunk_modifications = &user_client->previous_received_voxel_modifications[modified_chunk_ptr->index_of_modified_chunk /* Was filled in by flag_chunks_modified_by_client() */];
                
                    // Fill dummy voxels (temporarily)
                    s_fill_dummy_voxels(local_chunk_modifications);
                    {
                        for (uint32_t sm_voxel = 0; sm_voxel < voxel_delta->modified_chunks[modified_chunk_index].modified_voxel_count; ++sm_voxel) {
                            modified_voxel_t *sm_voxel_ptr = &voxel_delta->modified_chunks[modified_chunk_index].modified_voxels[sm_voxel];

                            voxel_coordinate_t coord = convert_1d_to_3d_coord(sm_voxel_ptr->index, CHUNK_EDGE_LENGTH);
                            if (dummy_voxels[coord.x][coord.y][coord.z] != 255) {
                                // Voxel has been modified
                                if (user_client->needs_to_do_voxel_correction) {
                                    if (dummy_voxels[coord.x][coord.y][coord.z] != sm_voxel_ptr->next_value) {
                                        // This happens elsewhere
                                    }
                                }
                            }
                            else {
                                modified_voxel_t *voxel_ptr = sm_voxel_ptr;
                                voxel_coordinate_t coord = convert_1d_to_3d_coord(voxel_ptr->index, CHUNK_EDGE_LENGTH);
                                modified_chunk_ptr->voxels[coord.x][coord.y][coord.z] = voxel_ptr->next_value;
                            }
                        }
                    }
                    // Unfill dummy voxels
                    s_unfill_dummy_voxels(local_chunk_modifications);
                }
                else {
                    // Just interpolate voxel values (simple)
                    for (uint32_t sm_voxel = 0; sm_voxel < voxel_delta->modified_chunks[modified_chunk_index].modified_voxel_count; ++sm_voxel) {
                        modified_voxel_t *voxel_ptr = &voxel_delta->modified_chunks[modified_chunk_index].modified_voxels[sm_voxel];
                        voxel_coordinate_t coord = convert_1d_to_3d_coord(voxel_ptr->index, CHUNK_EDGE_LENGTH);
                        modified_chunk_ptr->voxels[coord.x][coord.y][coord.z] = voxel_ptr->next_value;
                    }
                }

                ready_chunk_for_gpu_sync(modified_chunk_ptr);
            }
        }
        // Unflag chunks that have been modified by the client
        s_unflag_chunks_previously_modified_by_client(user_client);
    }
    else {
        //previous_voxel_delta_packet = nullptr;
    };

    clear_linear(&voxel_linear_allocator_front);
    if (get_previous_voxel_delta_packet()) {
        get_previous_voxel_delta_packet()->modified_count = 0;
    }

    elapsed_interpolation_time = 0.0f;
}


void ready_chunk_for_gpu_sync(chunk_t *chunk) {
    // If it is already scheduled for GPU sync, don't push to the update stack
    if (!chunk->should_do_gpu_sync) {
        chunks_to_gpu_sync[to_sync_count++] = convert_3d_to_1d_index(chunk->chunk_coord.x, chunk->chunk_coord.y, chunk->chunk_coord.z, grid_edge_size);
        chunk->should_do_gpu_sync = 1;
    }
}


void ready_chunk_for_gpu_sync(chunk_t *chunk, uint32_t chunk_index) {
    // If it is already scheduled for GPU sync, don't push to the update stack
    if (!chunk->should_do_gpu_sync) {
        chunks_to_gpu_sync[to_sync_count++] = chunk_index;
        chunk->should_do_gpu_sync = 1;
    }
}


void clear_chunk_history(void) {
    for (uint32_t i = 0; i < modified_chunks_count; ++i) {
        chunk_t *chunk = modified_chunks[i];

        for (uint32_t voxel = 0; voxel < chunk->modified_voxels_list_count; ++voxel) {
            uint16_t index = chunk->list_of_modified_voxels[voxel];
            chunk->voxel_history[index] = VOXEL_HAS_NOT_BEEN_APPENDED_TO_HISTORY /*255 - not been modified*/;
        }

        chunk->modified_voxels_list_count = 0;
        chunk->added_to_history = 0;
    }
    
    modified_chunks_count = 0;
}


void terraform_client(const ivector3_t &xs_voxel_coord, uint32_t voxel_radius, bool destructive, float32_t dt, float32_t speed) {
    ivector3_t voxel_coord = xs_voxel_coord;
    chunk_t *chunk = get_chunk_encompassing_point(voxel_coord);
    ready_chunk_for_gpu_sync(chunk);
    
    float32_t coefficient = (destructive ? -1.0f : 1.0f);
    
    float_t radius = (float32_t)voxel_radius;
    
    float32_t radius_squared = radius * radius;
    
    ivector3_t bottom_corner = voxel_coord - ivector3_t((uint32_t)radius);

    uint32_t diameter = (uint32_t)radius * 2 + 1;

    for (uint32_t z = 0; z < diameter; ++z) {
        for (uint32_t y = 0; y < diameter; ++y) {
            for (uint32_t x = 0; x < diameter; ++x) {
                vector3_t v_f = vector3_t(x, y, z) + vector3_t(bottom_corner);
                vector3_t diff = v_f - vector3_t(voxel_coord);
                float32_t real_distance_squared = glm::dot(diff, diff);
                if (real_distance_squared <= radius_squared) {
                    ivector3_t cs_vcoord = ivector3_t(v_f) - chunk->xs_bottom_corner;

                    if (is_within_boundaries(cs_vcoord, CHUNK_EDGE_LENGTH)) {
                        uint8_t *voxel = &chunk->voxels[(uint32_t)cs_vcoord.x][(uint32_t)cs_vcoord.y][(uint32_t)cs_vcoord.z];

                        float32_t proportion = 1.0f - (real_distance_squared / radius_squared);
                        
                        int32_t current_voxel_value = (int32_t)*voxel;
                        int32_t new_value = (int32_t)(proportion * coefficient * dt * speed) + current_voxel_value;

                        if (new_value > (int32_t)MAX_VOXEL_VALUE) {
                            *voxel = (uint8_t)MAX_VOXEL_VALUE;
                        }
                        else if (new_value < 0) {
                            *voxel = 0;
                        }
                        else {
                            *voxel = (uint8_t)new_value;
                        }
                    }
                    else {
                        chunk_t *new_chunk = get_chunk_encompassing_point(ivector3_t(v_f));
                        
                        if (new_chunk) {
                            chunk = new_chunk;

                            ready_chunk_for_gpu_sync(chunk);
                            cs_vcoord = ivector3_t(v_f) - chunk->xs_bottom_corner;
                        
                            uint8_t *voxel = &chunk->voxels[(uint32_t)cs_vcoord.x][(uint32_t)cs_vcoord.y][(uint32_t)cs_vcoord.z];

                            float32_t proportion = 1.0f - (real_distance_squared / radius_squared);
                            int32_t current_voxel_value = (int32_t)*voxel;
                            int32_t new_value = (int32_t)(proportion * coefficient * dt * speed) + current_voxel_value;

                            if (new_value > (int32_t)MAX_VOXEL_VALUE) {
                                *voxel = (uint8_t)MAX_VOXEL_VALUE;
                            }
                            else if (new_value < 0) {
                                *voxel = 0;
                            }
                            else {
                                *voxel = (uint8_t)new_value;
                            }
                        }
                    }
                }
            }
        }
    }

    ready_chunk_for_gpu_sync(chunk);
}


vector3_t ws_to_xs(const vector3_t &ws_position) {
    vector3_t voxel_world_origin = -vector3_t((float32_t)grid_edge_size / 2.0f) * (float32_t)(CHUNK_EDGE_LENGTH) * chunk_size;
    
    vector3_t from_origin = ws_position - voxel_world_origin;

    vector3_t xs_sized = from_origin / chunk_size;

    return xs_sized;
}


chunk_t **get_chunk(int32_t index) {
    static chunk_t *nul = nullptr;
    
    if (index == -1) {
        return(&nul);
    }
        
    return(&chunks[index]);
}


chunk_t **get_chunk(uint32_t x, uint32_t y, uint32_t z) {
    int32_t index = convert_3d_to_1d_index(x, y, z, grid_edge_size);
    
    static chunk_t *nul = nullptr;
    
    if (index == -1) {
        return(&nul);
    }
        
    return(&chunks[index]);
}


chunks_state_flags_t *get_chunks_state_flags(void) {
    return &flags;
}


chunk_t *get_chunk_encompassing_point(const vector3_t &xs_position) {
    ivector3_t rounded = ivector3_t(glm::round(xs_position));

    ivector3_t chunk_coord = rounded / (CHUNK_EDGE_LENGTH);

    return(*get_chunk(chunk_coord.x, chunk_coord.y, chunk_coord.z));
}


ivector3_t get_voxel_coord(const vector3_t &xs_position) {
    ivector3_t rounded = ivector3_t(glm::round(xs_position));
    ivector3_t cs_voxel_coord = ivector3_t(rounded.x % CHUNK_EDGE_LENGTH, rounded.y % (CHUNK_EDGE_LENGTH), rounded.z % (CHUNK_EDGE_LENGTH));
    return(cs_voxel_coord);
}


ivector3_t get_voxel_coord(const ivector3_t &xs_position) {
    ivector3_t cs_voxel_coord = ivector3_t(xs_position.x % CHUNK_EDGE_LENGTH, xs_position.y % (CHUNK_EDGE_LENGTH), xs_position.z % (CHUNK_EDGE_LENGTH));
    return(cs_voxel_coord);
}


game_snapshot_voxel_delta_packet_t *&get_previous_voxel_delta_packet(void) {
    return(previous_voxel_delta_packet_front);
}


chunk_t **get_modified_chunks(uint32_t *count) {
    *count = modified_chunks_count;
    return(modified_chunks);
}


float32_t get_chunk_grid_size(void) {
    return (float32_t)grid_edge_size;
}


float32_t get_chunk_size(void) {
    return chunk_size;
}


struct linear_allocator_t *get_voxel_linear_allocator(void) {
    return &voxel_linear_allocator_front;
}


// Static definitions
static void s_construct_plane(const vector3_t &ws_plane_origin, float32_t radius) {
    vector3_t xs_plane_origin = ws_to_xs(ws_plane_origin);

    chunk_t *chunk = get_chunk_encompassing_point(xs_plane_origin);
    
    ready_chunk_for_gpu_sync(chunk);
    
    ivector3_t plane_origin = ivector3_t(xs_plane_origin);

    radius /= chunk_size;
    radius = glm::round(radius);
    
    ivector3_t bottom_corner = plane_origin - ivector3_t((uint32_t)radius, 0, (uint32_t)radius);

    uint32_t diameter = (uint32_t)radius * 2 + 1;

    uint32_t y = 0;
    for (uint32_t z = 0; z < diameter; ++z) {
        for (uint32_t x = 0; x < diameter; ++x) {
            vector3_t v_f = vector3_t(x, y, z) + vector3_t(bottom_corner);

            ivector3_t cs_vcoord = ivector3_t(v_f) - chunk->xs_bottom_corner;

            if (is_within_boundaries(cs_vcoord, CHUNK_EDGE_LENGTH)) {
                chunk->voxels[(uint32_t)cs_vcoord.x][(uint32_t)cs_vcoord.y][(uint32_t)cs_vcoord.z] = (uint8_t)MAX_VOXEL_VALUE;
            }
            else {
                chunk = get_chunk_encompassing_point(ivector3_t(v_f));
                
                ready_chunk_for_gpu_sync(chunk);
                
                cs_vcoord = ivector3_t(v_f) - chunk->xs_bottom_corner;

                chunk->voxels[(uint32_t)cs_vcoord.x][(uint32_t)cs_vcoord.y][(uint32_t)cs_vcoord.z] = (uint8_t)MAX_VOXEL_VALUE;
            }
        }
    }
}


static void s_construct_sphere(const vector3_t &ws_sphere_position, float32_t radius) {
    vector3_t xs_sphere_position = ws_to_xs(ws_sphere_position);
    
    chunk_t *chunk = get_chunk_encompassing_point(xs_sphere_position);
    
    ready_chunk_for_gpu_sync(chunk);
    
    ivector3_t sphere_center = xs_sphere_position;

    radius /= chunk_size;
    radius = glm::round(radius);
    
    float32_t radius_squared = radius * radius;
    
    ivector3_t bottom_corner = sphere_center - ivector3_t((uint32_t)radius);

    uint32_t diameter = (uint32_t)radius * 2 + 1;

    for (uint32_t z = 0; z < diameter; ++z) {
        for (uint32_t y = 0; y < diameter; ++y) {
            for (uint32_t x = 0; x < diameter; ++x) {
                vector3_t v_f = vector3_t(x, y, z) + vector3_t(bottom_corner);
                vector3_t diff = v_f - vector3_t(sphere_center);
                float32_t real_distance_squared = glm::dot(diff, diff);

                if (real_distance_squared <= radius_squared) {
                    ivector3_t cs_vcoord = ivector3_t(v_f) - chunk->xs_bottom_corner;

                    if (is_within_boundaries(cs_vcoord, CHUNK_EDGE_LENGTH)) {
                        float32_t proportion = 1.0f - (real_distance_squared / radius_squared);
                        chunk->voxels[(uint32_t)cs_vcoord.x][(uint32_t)cs_vcoord.y][(uint32_t)cs_vcoord.z] = (uint32_t)((proportion) * (float32_t)MAX_VOXEL_VALUE);
                    }
                    else {
                        chunk = get_chunk_encompassing_point(ivector3_t(v_f));
                        
                        ready_chunk_for_gpu_sync(chunk);
                        
                        cs_vcoord = ivector3_t(v_f) - chunk->xs_bottom_corner;

                        float32_t proportion = 1.0f - (real_distance_squared / radius_squared);
                        
                        chunk->voxels[(uint32_t)cs_vcoord.x][(uint32_t)cs_vcoord.y][(uint32_t)cs_vcoord.z] = (uint32_t)((proportion) * (float32_t)MAX_VOXEL_VALUE);
                    }
                }
            }
        }
    }
}


static void s_terraform_with_history(const ivector3_t &xs_voxel_coord, uint32_t voxel_radius, bool destructive, float32_t dt, float32_t speed) {
    ivector3_t voxel_coord = xs_voxel_coord;
    chunk_t *chunk = get_chunk_encompassing_point(voxel_coord);
    uint8_t *history = chunk->voxel_history;
    
    ready_chunk_for_gpu_sync(chunk);
    
    float32_t coefficient = (destructive ? -1.0f : 1.0f);
    
    float_t radius = (float32_t)voxel_radius;
    
    float32_t radius_squared = radius * radius;
    
    ivector3_t bottom_corner = voxel_coord - ivector3_t((uint32_t)radius);

    uint32_t diameter = (uint32_t)radius * 2 + 1;

    s_append_chunk_to_history_of_modified_chunks_if_not_already(chunk);
    
    for (uint32_t z = 0; z < diameter; ++z) {
        for (uint32_t y = 0; y < diameter; ++y) {
            for (uint32_t x = 0; x < diameter; ++x) {
                vector3_t v_f = vector3_t(x, y, z) + vector3_t(bottom_corner);
                vector3_t diff = v_f - vector3_t(voxel_coord);
                float32_t real_distance_squared = glm::dot(diff, diff);
                if (real_distance_squared <= radius_squared) {
                    ivector3_t cs_vcoord = ivector3_t(v_f) - chunk->xs_bottom_corner;

                    if (is_within_boundaries(cs_vcoord, CHUNK_EDGE_LENGTH)) {
                        uint8_t *voxel = &chunk->voxels[(uint32_t)cs_vcoord.x][(uint32_t)cs_vcoord.y][(uint32_t)cs_vcoord.z];

                        float32_t proportion = 1.0f - (real_distance_squared / radius_squared);
                        
                        int32_t current_voxel_value = (int32_t)*voxel;
                        int32_t new_value = (int32_t)(proportion * coefficient * dt * speed) + current_voxel_value;

                        if (new_value > (int32_t)MAX_VOXEL_VALUE) {
                            *voxel = (int32_t)MAX_VOXEL_VALUE;
                        }
                        else if (new_value < 0) {
                            *voxel = 0;
                        }
                        else {
                            *voxel = (uint8_t)new_value;
                        }

                        uint8_t previous_voxel_value = (uint8_t)current_voxel_value;

                        int32_t index = convert_3d_to_1d_index((uint32_t)cs_vcoord.x, (uint32_t)cs_vcoord.y, (uint32_t)cs_vcoord.z, CHUNK_EDGE_LENGTH);
                        voxel_coordinate_t coord = convert_1d_to_3d_coord(index, CHUNK_EDGE_LENGTH);
                        
                        if (history[index] == VOXEL_HAS_NOT_BEEN_APPENDED_TO_HISTORY) {
                            history[index] = previous_voxel_value;
                            chunk->list_of_modified_voxels[chunk->modified_voxels_list_count++] = index;
                        }
                    }
                    else {
                        chunk_t *new_chunk = get_chunk_encompassing_point(ivector3_t(v_f));
                        
                        if (new_chunk) {
                            chunk = new_chunk;
                            history = (uint8_t *)chunk->voxel_history;

                            s_append_chunk_to_history_of_modified_chunks_if_not_already(chunk);
                            
                            ready_chunk_for_gpu_sync(chunk);
                            cs_vcoord = ivector3_t(v_f) - chunk->xs_bottom_corner;
                        
                            uint8_t *voxel = &chunk->voxels[(uint32_t)cs_vcoord.x][(uint32_t)cs_vcoord.y][(uint32_t)cs_vcoord.z];

                            float32_t proportion = 1.0f - (real_distance_squared / radius_squared);
                            int32_t current_voxel_value = (int32_t)*voxel;
                            int32_t new_value = (int32_t)(proportion * coefficient * dt * speed) + current_voxel_value;

                            if (new_value > (int32_t)MAX_VOXEL_VALUE) {
                                *voxel = (int32_t)MAX_VOXEL_VALUE;
                            }
                            else if (new_value < 0) {
                                *voxel = 0;
                            }
                            else {
                                *voxel = (uint8_t)new_value;
                            }

                            uint8_t previous_voxel_value = (uint8_t)current_voxel_value;

                            int32_t index = convert_3d_to_1d_index((uint32_t)cs_vcoord.x, (uint32_t)cs_vcoord.y, (uint32_t)cs_vcoord.z, CHUNK_EDGE_LENGTH);
                        
                            if (history[index] == VOXEL_HAS_NOT_BEEN_APPENDED_TO_HISTORY) {
                                history[index] = previous_voxel_value;
                                chunk->list_of_modified_voxels[chunk->modified_voxels_list_count++] = index;
                            }
                        }
                    }
                }
            }
        }
    }

    ready_chunk_for_gpu_sync(chunk);
}

static int32_t s_lua_create_sphere(lua_State *state) {
    int32_t radius = lua_tonumber(state, -1);
    
    if (editor_mode) {
        player_t *p = get_user_player();
        s_construct_sphere(p->ws_position, (float32_t)radius);
    }
    
    return 0;
}


static void s_clear_voxels() {
    for (uint32_t i = 0; i < chunks_to_render_count; ++i) {
        chunk_t *c = chunks_to_update[i];

        c->vertex_count = 0;
        memset(c->voxels, 0, sizeof(uint8_t) * CHUNK_EDGE_LENGTH * CHUNK_EDGE_LENGTH * CHUNK_EDGE_LENGTH);
    }

    chunks_to_render_count = 0;
}

static int32_t s_lua_save_map(lua_State *state) {
    const char *path = lua_tostring(state, -1);

    if (editor_mode) {
        map_data_t data = {};
        data.grid_edge_size = grid_edge_size;
        data.chunk_size = chunk_size;
        data.to_update_count = chunks_to_render_count;
        data.chunks = chunks;
        data.to_update = chunks_to_update;
        save_map(&data, path);
    }

    return 0;
}

static int32_t s_lua_clear_voxels(lua_State *state) {
    if (editor_mode) {
        s_clear_voxels();
    }

    return 0;
}


static void s_append_chunk_to_history_of_modified_chunks_if_not_already(chunk_t *chunk) {
    if (!chunk->added_to_history) {
        chunk->added_to_history = 1;
        modified_chunks[modified_chunks_count++] = chunk;
    }
}


static void s_flag_chunks_previously_modified_by_client(client_t *user_client) {
    if (user_client->modified_chunks_count == 0) {
        //output_to_debug_console("Client modified no chunks\n");
    }
    
    for (uint32_t previously_modified_chunk = 0; previously_modified_chunk < user_client->modified_chunks_count; ++previously_modified_chunk) {
        chunk_t *chunk = *get_chunk(user_client->previous_received_voxel_modifications[previously_modified_chunk].chunk_index);
        chunk->was_previously_modified_by_client = 1;
        chunk->index_of_modified_chunk = previously_modified_chunk;
    }
}


static void s_unflag_chunks_previously_modified_by_client(client_t *user_client) {
    // Flag chunks that have been modified by the client
    for (uint32_t previously_modified_chunk = 0; previously_modified_chunk < user_client->modified_chunks_count; ++previously_modified_chunk) {
        chunk_t *chunk = *get_chunk(user_client->previous_received_voxel_modifications[previously_modified_chunk].chunk_index);
        chunk->was_previously_modified_by_client = 0;
        chunk->index_of_modified_chunk = 0;
    }
}


static void s_fill_dummy_voxels(client_modified_chunk_nl_t *chunk) {
    for (uint32_t modified_voxel = 0; modified_voxel < chunk->modified_voxel_count; ++modified_voxel) {
        local_client_modified_voxel_t *voxel = &chunk->modified_voxels[modified_voxel];
        dummy_voxels[voxel->x][voxel->y][voxel->z] = 100;
    }
}


static void s_unfill_dummy_voxels(client_modified_chunk_nl_t *chunk) {
    for (uint32_t modified_voxel = 0; modified_voxel < chunk->modified_voxel_count; ++modified_voxel) {
        local_client_modified_voxel_t *voxel = &chunk->modified_voxels[modified_voxel];
        dummy_voxels[voxel->x][voxel->y][voxel->z] = 255;
    }
}
