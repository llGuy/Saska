#pragma once


#include "math.hpp"
#include "entity.hpp"
#include "graphics.hpp"


struct hitbox_t
{
    // Relative to the size of the entity
    // These are the values of when the entity size = 1
    float32_t x_max, x_min;
    float32_t y_max, y_min;
    float32_t z_max, z_min;
};


struct physics_component_create_info_t
{
    bool enabled;    
};


enum entity_physics_state_t { IN_AIR, ON_GROUND };


struct physics_component_t
{
    float32_t standing_movement_speed = 0.0f;
    
    bool enabled;
    hitbox_t hitbox;
    entity_physics_state_t state;

    vector3_t axes;
    float32_t acceleration = 2.0f;

    vector3_t previous_velocity;

    void tick(struct player_t *affected_player, float32_t dt);

private:
    void tick_standing_player_physics(struct player_t *player, float32_t dt);
    void tick_rolling_player_physics(struct player_t *player, float32_t dt);
    void tick_not_physically_affected_player(struct player_t *player, float32_t dt);
};


struct terraform_power_component_create_info_t
{
    float32_t speed;
    float32_t terraform_radius;
};


struct terraform_power_component_t
{
    // Some sort of limit or something
    float32_t speed;
    float32_t terraform_radius;

    void tick(struct player_t *affected_player, float32_t dt);
};


struct camera_component_create_info_t
{
    // From add_camera() function from graphics.hpp
    uint32_t camera_index;
    bool is_third_person;
    float32_t distance_from_player;
};


struct camera_component_t
{
    // Can be set to -1, in that case, there is no camera bound
    camera_handle_t camera{-1};

    static constexpr float32_t MAX_ANIMATION_TIME = 0.3f;
    bool changed_basis_vectors = false;
    float32_t animation_time = 0.0f;
    float32_t interpolation = 0.0f;
    vector3_t ws_next_vector = vector3_t(0, 1, 0);
    vector3_t ws_current_up_vector = vector3_t(0, 1, 0);
    
    // When for example, the layer is standing somewhere with different surface normal (when in rolling mode only)
    uint8_t is_in_rotation_animation: 1;
    uint8_t is_third_person: 1;
    uint8_t initialized_previous_position: 1;
    
    float32_t distance_from_player = 15.0f;

    vector3_t previous_position = vector3_t(0.0f);
    
    vector2_t mouse_diff = vector2_t(0.0f);

    smooth_exponential_interpolation_t<float32_t, smooth_exponential_interpolation_compare_float_t> fov;
    smooth_exponential_interpolation_t<float32_t, smooth_exponential_interpolation_compare_float_t> camera_distance;

    smooth_linear_interpolation_t<float32_t> transition_first_third = {};

    void tick(struct player_t *affected_player, float32_t dt);
};


struct animation_component_create_info_t
{
    uniform_layout_t *ubo_layout;
    skeleton_t *skeleton;
    animation_cycles_t *cycles;
};


struct animation_component_t
{
    // Rendering the animated entity
    animated_instance_t animation_instance;
    animation_cycles_t *cycles;

    void tick(struct player_t *affected_player, float32_t dt);
};


struct rendering_component_create_info_t
{
};


struct rendering_component_t
{
    // push constant stuff for the graphics pipeline
    struct
    {
	// in world space
	matrix4_t ws_t{1.0f};
	vector4_t color;

        float32_t roughness;
        float32_t metalness;
    } push_k;

    struct
    {
        matrix4_t ws_t{1.0f};
        vector4_t color;

        float32_t fade = 0.5f;
    } push_k_alpha;

    bool enabled = true;

    void tick(struct player_t *affected_player, float32_t dt);
    void tick(struct bullet_t *affected_bullet, float32_t dt);
};


struct shoot_component_create_info_t
{
    float32_t cool_off;
    float32_t shoot_speed;
};


struct shoot_component_t
{
    float32_t cool_off;
    float32_t shoot_speed;

    void tick(struct player_t *affected_player, float32_t dt);
};


struct burnable_component_t
{
    bool burning = 0;
    // Going to have to update this every frame
    int32_t particle_index = 0;

    void tick(struct bullet_t *affected_bullet, float32_t dt);
    void set_on_fire(const vector3_t &position);
    void extinguish_fire(void);
};


struct entity_body_t
{
    float32_t weight = 1.0f;
    hitbox_t hitbox;
};


struct network_component_create_info_t
{
    uint32_t entity_index;
    uint32_t client_state_index;
};


#define MAX_PLAYER_STATES 40


struct remote_player_snapshot_t
{
    vector3_t ws_position;
    vector3_t ws_direction;
    vector3_t ws_up_vector;
    quaternion_t ws_rotation;
    uint32_t action_flags;

    union
    {
        struct
        {
            uint8_t rolling_mode: 1;
            // ...
        };
        uint8_t flags;
    };
};


// What is player doing, how has it changed (mouse movement, etc..)
struct player_state_t
{
    uint32_t action_flags;
    float32_t mouse_x_diff;
    float32_t mouse_y_diff;
    // Flags
    union
    {
        struct
        {
            uint8_t is_entering: 1;
            uint8_t rolling_mode: 1;
            uint8_t physics_state: 2;
        };
        uint8_t flags_byte;
    };

    vector3_t ws_position;
    vector3_t ws_direction;
    vector3_t ws_velocity;
    uint64_t tick;

    // For testing only:
    uint64_t current_state_count = 0;

    float32_t dt;
};


struct network_component_t
{
    uint32_t commands_to_flush = 0;
    
    // May be different from client to client
    uint32_t entity_index;
    // Is the same from client to client
    uint32_t client_state_index;

    // This will only be allocated for servers
    circular_buffer_t<struct player_state_t> player_states_cbuffer;

    // Stuff for remote players
    struct
    {
        // Rendering time for the remote players will be 100ms behind (two snapshots - TODO: Make this depend on the snapshot rate of the server)
        
        // This will only be allocated for remote players
        circular_buffer_t<struct remote_player_snapshot_t> remote_player_states;
        // Previous = remote_player_states.tail

        float32_t elapsed_time = 0.0f;
        float32_t max_time = 0.0f /* Set this depending on the snapshot rate of the server snapshot rate */;
    };

    bool is_remote = 0;

    float32_t tick(struct player_t *affceted_player, float32_t dt);
};


struct bounce_physics_component_create_info_t
{
};


struct bounce_physics_component_t
{
    // Data
    void tick(struct bullet_t *affected_bullet, float32_t dt);
};
