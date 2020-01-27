#pragma once

#include "string.hpp"
#include "entity.hpp"
#include "component.hpp"


// Action components can be modified over keyboard / mouse input, or on a network
enum action_flags_t { ACTION_FORWARD, ACTION_LEFT, ACTION_BACK, ACTION_RIGHT, ACTION_UP, ACTION_DOWN, ACTION_RUN, ACTION_SHOOT, ACTION_TERRAFORM_DESTROY, ACTION_TERRAFORM_ADD, SHOOT };
enum player_color_t { BLUE, RED, GRAY, DARK_GRAY, GREEN, ORANGE, INVALID_COLOR };


using player_handle_t = int32_t;


struct player_create_info_t
{
    constant_string_t name;
    vector3_t ws_position;
    vector3_t ws_direction;
    float32_t starting_velocity;
    quaternion_t ws_rotation;
    vector3_t ws_size;
    player_color_t color;
    physics_component_create_info_t physics_info;
    terraform_power_component_create_info_t terraform_power_info;
    camera_component_create_info_t camera_info;
    animation_component_create_info_t animation_info;
    rendering_component_create_info_t rendering_info;
    shoot_component_create_info_t shoot_info;
    network_component_create_info_t network_info;
};


struct player_t : entity_t
{
    constant_string_t id;
    
    vector3_t surface_normal;
    vector3_t surface_position;

    float32_t current_rotation_speed = 0;
    float32_t current_rolling_rotation_angle = 0;
    vector3_t rolling_rotation_axis = vector3_t(1, 0, 0);
    matrix4_t rolling_rotation = matrix4_t(1.0f);

    uint32_t action_flags = 0;
    uint32_t previous_action_flags = 0;
    
    // For animated rendering component
    enum animated_state_t { WALK, IDLE, RUN, HOVER, SLIDING_NOT_ROLLING_MODE, SITTING, JUMP } animated_state = animated_state_t::IDLE;
    
    player_handle_t index;

    // Flags
    uint32_t is_entering: 1; // When entering, is in "meteor" state, can control movement in air
    uint32_t is_in_air: 1;
    uint32_t is_sitting: 1;
    uint32_t is_sliding_not_rolling_mode: 1;
    uint32_t toggled_rolling_previous_frame: 1;
    uint32_t rolling_mode: 1;

    float32_t entering_acceleration = 0.0f;
    
    camera_component_t camera;
    physics_component_t physics;
    rendering_component_t rendering;
    animation_component_t animation;
    network_component_t network;
    terraform_power_component_t terraform_power;
    shoot_component_t shoot;



    void initialize(player_create_info_t *info);
    player_state_t create_player_state(void);
};
