#include "gamestate.hpp"
#include "game.hpp"

#include "chunks_gstate.hpp"
#include "entities_gstate.hpp"
#include "particles_gstate.hpp"


// Global
static bool initialized_world;
static uint64_t current_tick;



static void render_world(uint32_t image_index, uint32_t current_frame, gpu_command_queue_t *queue);



void initialize_gamestate(struct raw_input_t *raw_input)
{
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


void tick_gamestate(struct game_input_t *game_input, float32_t dt, gpu_command_queue_t *queue, enum application_type_t app_type, enum element_focus_t focus, uint32_t image_index)
{
    tick_chunks_state(dt);
    tick_entities_state(game_input, dt, app_type);
    tick_particles_state(dt);

    // GPU operations are separate from ticking
    update_3d_output_camera_transforms(image_index);
    sync_gpu_with_entities_state(queue);
    sync_gpu_with_chunks_state(queue);
    sync_gpu_with_particles_state(queue);
    
    render_world(image_index, 0, queue);

    // Increment current tick
    ++(current_tick);
}


uint64_t *get_current_tick(void)
{
    return(&current_tick);
}



static void render_world(uint32_t image_index, uint32_t current_frame, gpu_command_queue_t *queue)
{
    // Fetch some data needed to render
    auto transforms_ubo_uniform_groups = get_camera_transform_uniform_groups();
    shadow_display_t shadow_display_data = get_shadow_display();
    
    uniform_group_t uniform_groups[2] = {transforms_ubo_uniform_groups[image_index], shadow_display_data.texture};

    camera_t *camera = get_camera_bound_to_3d_output();

    // Rendering to the shadow map
    begin_shadow_offscreen(lighting_t::shadows_t::SHADOWMAP_W, lighting_t::shadows_t::SHADOWMAP_H, queue);
    {
        render_entities_to_shadowmap(&transforms_ubo_uniform_groups[image_index], queue);
        render_chunks_to_shadowmap(&transforms_ubo_uniform_groups[image_index], queue);
    }
    end_shadow_offscreen(queue);

    // Rendering the scene with lighting and everything
    begin_deferred_rendering(image_index, queue);
    {
        render_entities(uniform_groups, queue);
        render_chunks(uniform_groups, queue);

        render_atmosphere({1, uniform_groups}, camera->p, queue);
        render_sun(uniform_groups, queue);
    }
    do_lighting_and_transition_to_alpha_rendering(camera->v_m, queue);
    {
        // Render particles
        // TODO: In future, render skybox and sun here
        render_alpha_particles(uniform_groups, queue);
        render_transparent_entities(uniform_groups, queue);
    }
    end_deferred_rendering(queue);

    apply_pfx_on_scene(queue, &transforms_ubo_uniform_groups[image_index], camera->v_m, camera->p_m);
}
