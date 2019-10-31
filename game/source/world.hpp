/* world.hpp */

#pragma once

#include "core.hpp"
#include "vulkan.hpp"
#include "graphics.hpp"
#include <glm/glm.hpp>
#include <glm/gtx/transform.hpp>

#define MAX_PLAYERS 30
#define MAX_BULLETS 100
#define VOXEL_CHUNK_EDGE_LENGTH 16
#define MAX_VERTICES_PER_VOXEL_CHUNK 5 * (VOXEL_CHUNK_EDGE_LENGTH - 1) * (VOXEL_CHUNK_EDGE_LENGTH - 1) * (VOXEL_CHUNK_EDGE_LENGTH - 1)

// Will always be allocated on the heap
struct voxel_chunk_t
{
    ivector3_t xs_bottom_corner;
    ivector3_t chunk_coord;
    
    uint8_t voxels[VOXEL_CHUNK_EDGE_LENGTH][VOXEL_CHUNK_EDGE_LENGTH][VOXEL_CHUNK_EDGE_LENGTH];

    uint32_t vertex_count;
    vector3_t mesh_vertices[MAX_VERTICES_PER_VOXEL_CHUNK];

    // Chunk rendering data
    mesh_t gpu_mesh;
    gpu_buffer_t chunk_mesh_gpu_buffer;

    struct push_k // Rendering stuff
    {
        matrix4_t model_matrix;
        vector4_t color;
    } push_k;

    bool should_do_gpu_sync = 0;
};

void ready_chunk_for_gpu_sync(voxel_chunk_t *chunk);
void initialize_chunk(voxel_chunk_t *chunk, vector3_t position);
void update_chunk_mesh(voxel_chunk_t *chunk, uint8_t surface_level);
void push_chunk_to_render_queue(voxel_chunk_t *chunk);
voxel_chunk_t **get_voxel_chunk(int32_t index);

struct voxel_chunks_t
{
    // How many chunks on the x, y and z axis
    uint32_t grid_edge_size;
    float32_t size;
    
    uint32_t chunk_count;
    uint32_t max_chunks;
    // Will be an array of pointers to the actual chunk data
    voxel_chunk_t **chunks;

    // Information that graphics pipelines need
    model_t chunk_model;

    pipeline_handle_t chunk_pipeline;
    pipeline_handle_t dbg_chunk_edge_pipeline;

    pipeline_handle_t chunk_mesh_pipeline;
    pipeline_handle_t chunk_mesh_shadow_pipeline;

    gpu_material_submission_queue_t gpu_queue;


    uint32_t to_sync_count = 0;
    uint32_t chunks_to_gpu_sync[20];
};

using player_handle_t = int32_t;

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
    
    float32_t distance_from_player = 40.0f;
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

    bool enabled = true;
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
};

struct entity_body_t
{
    float32_t weight = 1.0f;
    hitbox_t hitbox;
};

// Action components can be modified over keyboard / mouse input, or on a network
enum action_flags_t { ACTION_FORWARD, ACTION_LEFT, ACTION_BACK, ACTION_RIGHT, ACTION_UP, ACTION_DOWN, ACTION_RUN, ACTION_SHOOT, ACTION_TERRAFORM_DESTROY, ACTION_TERRAFORM_ADD, SHOOT };

struct network_component_t
{
    uint32_t client_state_index;
};


enum player_color_t { BLUE, RED, GRAY, DARK_GRAY, GREEN, INVALID_COLOR };

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
};

struct entity_t
{
    constant_string_t id {""_hash};
    // position, direction, velocity
    // in above entity group space
    vector3_t ws_up = vector3_t(0, 1, 0);
    vector3_t ws_p{0.0f}, ws_d{0.0f}, ws_v{0.0f}, ws_input_v{0.0f};
    quaternion_t ws_r{0.0f, 0.0f, 0.0f, 0.0f};
    vector3_t size{1.0f};
};

struct player_t : entity_t
{
    vector3_t surface_normal;
    vector3_t surface_position;

    float32_t current_rotation_speed = 0;
    float32_t current_rolling_rotation_angle = 0;
    vector3_t rolling_rotation_axis = vector3_t(1, 0, 0);
    matrix4_t rolling_rotation = matrix4_t(1.0f);

    uint32_t action_flags = 0;
    
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
};


struct bounce_physics_component_t
{
    
};

struct bullet_t : entity_t
{
    rendering_component_t rendering;
    bounce_physics_component_t bounce_physics;
};


struct dbg_entities_t
{
    bool hit_box_display = false;
    player_t *render_sliding_vector_player = nullptr;
};


struct entities_t
{
    dbg_entities_t dbg;

    
    int32_t player_count = 0;
    player_t player_list[MAX_PLAYERS];


    int32_t bullet_count = 0;
    bullet_t bullet_list[MAX_BULLETS];

    
    struct hash_table_inline_t<player_handle_t, 30, 5, 5> name_map{"map.entities"};


    pipeline_handle_t player_ppln;
    pipeline_handle_t player_shadow_ppln;

    
    pipeline_handle_t rolling_player_ppln;
    pipeline_handle_t rolling_player_shadow_ppln;

    
    pipeline_handle_t dbg_hitbox_ppln;


    mesh_t rolling_player_mesh;
    model_t rolling_player_model;

    
    mesh_t player_mesh;
    skeleton_t player_mesh_skeleton;
    animation_cycles_t player_mesh_cycles;
    uniform_layout_t animation_ubo_layout;
    model_t player_model;

    
    // For now:
    int32_t main_player = -1;
    // have some sort of stack of REMOVED entities

    gpu_material_submission_queue_t player_submission_queue;
    gpu_material_submission_queue_t rolling_player_submission_queue;
};

struct particles_t
{
    particle_spawner_t explosion_particle_spawner;
};

struct world_t
{
    struct entities_t entities;
    struct voxel_chunks_t voxel_chunks;
    struct particles_t particles;
};

player_t *get_player(player_handle_t player_handle);
voxel_chunk_t **get_voxel_chunk(uint32_t index);

void spawn_explosion(const vector3_t &position);
player_handle_t spawn_player(const char *player_name, player_color_t color);
player_handle_t spawn_player_at(const char *player_name, player_color_t color, const vector3_t &ws_position, const quaternion_t &quat);
void make_player_renderable(player_handle_t player_handle, player_color_t color);
void make_player_main(player_handle_t player_handle, input_state_t *input_state);

void update_network_world_state(void);

void clean_up_world_data(void);
void make_world_data(void);

void set_focus_for_world(void);
void remove_focus_for_world(void);

void hard_initialize_world(input_state_t *input_state, VkCommandPool *cmdpool, enum application_type_t app_type, enum application_mode_t app_mode);
void initialize_world(input_state_t *input_state, VkCommandPool *cmdpool, enum application_type_t type, enum application_mode_t mode);
void update_world(input_state_t *input_state, float32_t dt, uint32_t image_index, uint32_t current_frame, gpu_command_queue_t *queue, enum application_type_t type, enum element_focus_t focus);
void handle_world_input(input_state_t *input_state, float32_t dt);
void handle_input_debug(input_state_t *input_state, float32_t dt);
void destroy_world(void);
void initialize_world_translation_unit(struct game_memory_t *memory);
