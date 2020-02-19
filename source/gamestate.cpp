#include "gamestate.hpp"
#include "game.hpp"

#include "chunks_gstate.hpp"
#include "entities_gstate.hpp"
#include "particles_gstate.hpp"

#include "atmosphere.hpp"
#include "deferred_renderer.hpp"

#include "event_system.hpp"


// Global
static bool initialized_world;
static uint64_t current_tick;



static void s_render_world(gpu_command_queue_t *queue);


struct gamestate_event_data_cache_t
{
};

static gamestate_event_data_cache_t event_data_cache;

static void s_gamestate_event_callback(void *object, event_t *e)
{
    switch(e->type)
    {

    case event_type_t::LAUNCH_MAP_EDITOR:
    {
        auto *data = (event_data_launch_map_editor_t *)e->data;

        deinitialize_entities_state();
        deinitialize_chunks_state();

        uint32_t pre_length = (uint32_t)strlen("maps/");
        uint32_t name_length = (uint32_t)strlen(data->map_name);
        uint32_t post_length = (uint32_t)strlen(".map");
        uint32_t path_length = pre_length + name_length + post_length;
        auto *full_path = LN_MALLOC(char, path_length + 1);
        memcpy_s(full_path, pre_length, "maps/", pre_length);
        memcpy_s(full_path + pre_length, name_length, data->map_name, name_length);
        memcpy_s(full_path + pre_length + name_length, post_length, ".map", post_length);
        full_path[path_length] = 0;
        populate_chunks_state(full_path);

        clear_and_request_focus(element_focus_t::WORLD_3D_ELEMENT_FOCUS);
    } break;

    }
}


void initialize_gamestate(struct raw_input_t *raw_input, event_dispatcher_t *dispatcher)
{
    // Subscribe to events
    dispatcher->set_callback(listener_t::WORLD, &s_gamestate_event_callback, &event_data_cache);
    dispatcher->subscribe(event_type_t::LAUNCH_MAP_EDITOR, listener_t::WORLD);
    
    initialize_entities_state();
    initialize_chunks_state();
    initialize_particles_state();

    clear_linear();

    update_spectator_camera(glm::lookAt(vector3_t(-140.0f, 140.0f, -140.0f), glm::normalize(vector3_t(1.0f, -1.0f, 1.0f)) + vector3_t(-140.0f, 140.0f, -140.0f), vector3_t(0.0f, 1.0f, 0.0f)));
}


void populate_gamestate(game_state_initialize_packet_t *packet, struct raw_input_t *raw_input)
{
    current_tick = 0;

    populate_entities_state(packet, raw_input);
    populate_chunks_state(packet);
}


void deinitialize_gamestate(void)
{
    deinitialize_chunks_state();
    deinitialize_entities_state();
}


void fill_game_state_initialize_packet(game_state_initialize_packet_t *packet, player_handle_t new_client_index)
{
    fill_game_state_initialize_packet_with_chunk_state(packet);

    fill_game_state_initialize_packet_with_entities_state(packet, new_client_index);
}


void tick_gamestate(struct game_input_t *game_input, float32_t dt, gpu_command_queue_t *queue, enum application_type_t app_type, enum element_focus_t focus)
{
    tick_chunks_state(dt);
    tick_entities_state(game_input, dt, app_type);
    tick_particles_state(dt);

    // GPU operations are separate from ticking
    switch (get_app_type())
    {
    case application_type_t::WINDOW_APPLICATION_MODE: {
        update_lighting();
        update_camera_transforms(queue);

        sync_gpu_with_entities_state(queue);
        sync_gpu_with_chunks_state(queue);
        sync_gpu_with_particles_state(queue);


        s_render_world(queue);
    } break;

    default: {
        sync_gpu_with_chunks_state(queue);
    } break;
    }

    // Increment current tick
    ++(current_tick);
}


uint64_t *get_current_tick(void)
{
    return(&current_tick);
}



static void s_render_world(gpu_command_queue_t *queue)
{
    // Fetch some data needed to render
    auto transforms_ubo_uniform_group = camera_transforms_uniform();
    shadow_display_t shadow_display_data = get_shadow_display();
    
    uniform_group_t uniform_groups[2] = {transforms_ubo_uniform_group, shadow_display_data.texture};

    camera_t *camera = camera_bound_to_3d_output();

    // Rendering to the shadow map
    begin_shadow_offscreen(queue);
    {
        render_entities_to_shadowmap(&transforms_ubo_uniform_group, queue);
        render_chunks_to_shadowmap(&transforms_ubo_uniform_group, queue);
    }
    end_shadow_offscreen(queue);

    // Rendering the scene with lighting and everything
    begin_deferred_rendering(queue);
    {
        render_entities(uniform_groups, queue);
        render_chunks(uniform_groups, queue);

        render_atmosphere(&transforms_ubo_uniform_group, camera->p, queue);
        render_sun(uniform_groups, queue);
    }
    do_lighting_and_begin_alpha_rendering(get_sun(), camera->v_m, queue);
    {
        // Render particles
        // TODO: In future, render skybox and sun here
        render_alpha_particles(uniform_groups, queue);
        render_transparent_entities(uniform_groups, queue);
    }
    end_deferred_rendering(queue);

    apply_pfx_on_scene(queue, &transforms_ubo_uniform_group, camera->v_m, camera->p_m);
}
