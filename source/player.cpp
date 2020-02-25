#include "math.hpp"
#include "game.hpp"
#include "player.hpp"
#include "graphics.hpp"

void player_t::initialize(player_create_info_t *info) {
    static vector4_t colors[player_color_t::INVALID_COLOR] = { vector4_t(0.0f, 0.0f, 0.7f, 1.0f),
                                                               vector4_t(0.7f, 0.0f, 0.0f, 1.0f),
                                                               vector4_t(0.4f, 0.4f, 0.4f, 1.0f),
                                                               vector4_t(0.0f, 0.0f, 0.0f, 1.0f),
                                                               vector4_t(0.0f, 0.7f, 0.0f, 1.0f),
                                                               vector4_t(222.0f, 88.0f, 36.0f, 256.0f) / 256.0f };
    
    id = info->name;
    ws_position = info->ws_position;
    ws_direction = info->ws_direction;
    entering_acceleration = info->starting_velocity;
    size = info->ws_size;
    ws_rotation = info->ws_rotation;
    is_entering = 1;
    is_sitting = 0;
    is_in_air = 0;
    is_sliding_not_rolling_mode = 0;
    rolling_mode = 1;
    camera.camera = info->camera_info.camera_index;
    camera.is_third_person = info->camera_info.is_third_person;
    camera.distance_from_player = info->camera_info.distance_from_player;
    camera.initialized_previous_position = 0;

    // When game starts, camera distance is going from normal to far, when player lands, camera distance will go from far to normal
    camera.camera_distance.in_animation = 1;
    camera.camera_distance.destination = camera.distance_from_player * 1.3f;
    camera.camera_distance.current = camera.distance_from_player;
    camera.camera_distance.compare = &smooth_exponential_interpolation_compare_float;
    camera.camera_distance.speed = 3.0f;

    camera.fov.in_animation = 1;
    camera.fov.destination = 1.3f;
    camera.fov.current = 1.0f;
    camera.fov.compare = &smooth_exponential_interpolation_compare_float;
    camera.fov.speed = 3.0f;
    
    
    physics.enabled = info->physics_info.enabled;
    animation.cycles = info->animation_info.cycles;

    switch (get_app_type()) {
    case application_type_t::WINDOW_APPLICATION_MODE: {
        animation.animation_instance = initialize_animated_instance(get_global_command_pool(), info->animation_info.ubo_layout, info->animation_info.skeleton, info->animation_info.cycles);
        switch_to_cycle(&animation.animation_instance, player_t::animated_state_t::IDLE, 1);
    } break;
    }
    
    rendering.push_k.color = colors[info->color];
    rendering.push_k.roughness = 0.0f;
    rendering.push_k.metalness = 0.8f;
    terraform_power.speed = info->terraform_power_info.speed;
    shoot.cool_off = info->shoot_info.cool_off;
    shoot.shoot_speed = info->shoot_info.shoot_speed;
    network.entity_index = info->network_info.entity_index;
    network.client_state_index = info->network_info.client_state_index;
    network.player_states_cbuffer.initialize(MAX_PLAYER_STATES);
    network.remote_player_states.initialize(MAX_PLAYER_STATES);
}


player_state_t player_t::create_player_state(void) {
    player_state_t state = {};
    state.action_flags = action_flags;
    state.mouse_x_diff = camera.mouse_diff.x;
    state.mouse_y_diff = camera.mouse_diff.y;

    state.is_entering = is_entering;
    state.rolling_mode = rolling_mode;
    state.ws_position = ws_position;
    state.ws_direction = ws_direction;
    state.ws_velocity = ws_velocity;

    return(state);
}
