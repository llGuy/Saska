/* world.hpp */

#pragma once

#include "core.hpp"
#include "vulkan.hpp"
#include "graphics.hpp"
#include <glm/glm.hpp>
#include "raw_input.hpp"
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

    // Make the maximum voxel value be 254 (255 will be reserved for *not* modified in the second section of the 2-byte voxel value)
    uint8_t voxels[VOXEL_CHUNK_EDGE_LENGTH][VOXEL_CHUNK_EDGE_LENGTH][VOXEL_CHUNK_EDGE_LENGTH];

    // These will be for the server
    uint8_t *voxel_history = nullptr; // Array size will be VOXEL_CHUNK_EDGE_LENGTH ^ 3
    static constexpr uint32_t MAX_MODIFIED_VOXELS = (VOXEL_CHUNK_EDGE_LENGTH * VOXEL_CHUNK_EDGE_LENGTH * VOXEL_CHUNK_EDGE_LENGTH) / 4;
    uint32_t modified_voxels_list_count = 0;
    uint16_t *list_of_modified_voxels = nullptr;

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

    bool added_to_history = 0;

    union
    {
        // Flags and stuff
        uint32_t flags;
        struct
        {
            uint32_t was_previously_modified_by_client: 1;
            uint32_t index_of_modified_chunk: 31;
        };
    };
};

void ready_chunk_for_gpu_sync(voxel_chunk_t *chunk);
void initialize_chunk(voxel_chunk_t *chunk, vector3_t position);
void update_chunk_mesh(voxel_chunk_t *chunk, uint8_t surface_level);
void push_chunk_to_render_queue(voxel_chunk_t *chunk);
voxel_chunk_t **get_voxel_chunk(int32_t index);
voxel_chunk_t **get_voxel_chunk(uint32_t x, uint32_t y, uint32_t z);

struct voxel_chunks_flags_t
{
    // Should not be updating if 
    uint32_t should_update_chunk_meshes_from_now: 1;
    // Number of chunks to update received from server
    uint32_t chunks_received_to_update_count: 7;
    // Number that the client is waiting for
    uint32_t chunks_to_be_received: 8;
};

struct voxel_chunks_t
{
    uint8_t dummy_voxels[VOXEL_CHUNK_EDGE_LENGTH][VOXEL_CHUNK_EDGE_LENGTH][VOXEL_CHUNK_EDGE_LENGTH];
    
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
    uint32_t chunks_to_render_count = 0;
    voxel_chunk_t **chunks_to_update;

    uint32_t to_sync_count = 0;
    uint32_t chunks_to_gpu_sync[20];



    // For the server: in between every game snapshot dispatch, server queues up all terraforming actions.
    // TODO: MAKE IT SO THAT THIS ONLY GETS ALLOCATED FOR THE SERVER!!!
    static constexpr uint32_t MAX_MODIFIED_CHUNKS = 32;
    uint32_t modified_chunks_count = 0;
    voxel_chunk_t *modified_chunks[MAX_MODIFIED_CHUNKS] = {};
    float32_t elapsed_interpolation_time = 0.0f;


    // Flags
    voxel_chunks_flags_t flags;
    

    // This is used for interpolating between snapshots (maximum size is 5000 bytes)
    linear_allocator_t voxel_linear_allocator_front = {};
    struct game_snapshot_voxel_delta_packet_t *previous_voxel_delta_packet_front = nullptr;

    linear_allocator_t voxel_linear_allocator_back = {};
    struct game_snapshot_voxel_delta_packet_t *previous_voxel_delta_packet_back = nullptr;
};

voxel_chunks_flags_t *get_voxel_chunks_flags(void);

void reset_voxel_interpolation(void);
linear_allocator_t *get_voxel_linear_allocator(void);
game_snapshot_voxel_delta_packet_t *&get_previous_voxel_delta_packet(void);

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

// Linear interpolation (constant speed)
template <typename T> struct smooth_linear_interpolation_t
{
    bool in_animation;

    T current;
    T prev;
    T next;
    float32_t current_time;
    float32_t max_time;

    void animate(float32_t dt)
    {
        if (in_animation)
        {
            current_time += dt;
            float32_t progression = current_time / max_time;
        
            if (progression >= 1.0f)
            {
                in_animation = 0;
                current = next;
            }
            else
            {
                current = prev + progression * (next - prev);
            }
        }
    }
};

typedef bool (*smooth_exponential_interpolation_compare_float_t)(float32_t current, float32_t destination);
inline bool smooth_exponential_interpolation_compare_float(float32_t current, float32_t destination)
{
    return(abs(destination - current) < 0.001f);
}

// Starts fast, then slows down
template <typename T, typename Compare> struct smooth_exponential_interpolation_t
{
    bool in_animation;
    
    T destination;
    T current;
    float32_t speed = 1.0f;

    Compare compare;

    void animate(float32_t dt)
    {
        if (in_animation)
        {
            current += (destination - current) * dt * speed;
            if (compare(current, destination))
            {
                in_animation = 0;
                current = destination;
            }
        }
    }
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

struct burnable_component_t
{
    bool burning = 0;
    // Going to have to update this every frame
    int32_t particle_index = 0;
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
};


// Action components can be modified over keyboard / mouse input, or on a network
enum action_flags_t { ACTION_FORWARD, ACTION_LEFT, ACTION_BACK, ACTION_RIGHT, ACTION_UP, ACTION_DOWN, ACTION_RUN, ACTION_SHOOT, ACTION_TERRAFORM_DESTROY, ACTION_TERRAFORM_ADD, SHOOT };
enum player_color_t { BLUE, RED, GRAY, DARK_GRAY, GREEN, ORANGE, INVALID_COLOR };

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

struct entity_t
{
    bool dead = false;
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
        };
        uint8_t flags_byte;
    };

    vector3_t ws_position;
    vector3_t ws_direction;
    vector3_t ws_velocity;
    uint64_t tick;
    float32_t dt;
};


player_state_t initialize_player_state(player_t *player);


struct bounce_physics_component_create_info_t
{
};


struct bounce_physics_component_t
{
    // Data
};


struct bullet_create_info_t
{
    vector3_t ws_position;
    vector3_t ws_direction;
    quaternion_t ws_rotation;
    vector3_t ws_size;
    player_color_t color;
    bounce_physics_component_create_info_t bounce_info;
    rendering_component_create_info_t rendering_info;
};


struct bullet_t : entity_t
{
    uint32_t player_index;
    rendering_component_t rendering;
    bounce_physics_component_t bounce_physics;
    burnable_component_t burnable;
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
    uint32_t removed_bullets_stack_head;
    uint16_t removed_bullets_stack[MAX_BULLETS];

    
    hash_table_inline_t<player_handle_t, 30, 5, 5> name_map{"map.entities"};


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
    particle_spawner_t fire_particle_spawner;
};

struct world_t
{
    struct entities_t entities;
    struct voxel_chunks_t voxel_chunks;
    struct particles_t particles;

    // Not hard initialize (rendering state, vulkan objects, shaders...) but just initialize game data like initialized entities, voxels, etc...
    bool initialized_world;


    // Gets incremented every frame
    uint64_t current_tick_id = 0;
};

// Gets the data of the player that is being controlled by client
player_t *get_user_player(void);
player_t *get_player(const char *name);
player_t *get_player(player_handle_t player_handle);
voxel_chunk_t **get_voxel_chunk(uint32_t index);

player_handle_t initialize_player_from_player_init_packet(uint32_t local_user_client_index, struct player_state_initialize_packet_t *player_init_packet, camera_handle_t camera /* Optional */ = 0);
uint32_t spawn_fire(const vector3_t &position);
void spawn_explosion(const vector3_t &position);
void spawn_bullet(player_t *shooter);
player_handle_t spawn_player(const char *player_name, player_color_t color, uint32_t client_id);
player_handle_t spawn_player_at(const char *player_name, player_color_t color, const vector3_t &ws_position, const quaternion_t &quat);
void make_player_renderable(player_handle_t player_handle, player_color_t color);
void make_player_main(player_handle_t player_handle, raw_input_t *raw_input);

void update_network_world_state(void);
void update_networked_player(uint32_t player_index);
void initialize_game_state_initialize_packet(struct game_state_initialize_packet_t *packet, player_handle_t new_client_handle);
// Will create packet for each chunk
struct voxel_chunk_values_packet_t *initialize_chunk_values_packets(uint32_t *count);
void clear_chunk_history_for_server(void);
voxel_chunk_t **get_modified_voxel_chunks(uint32_t *count);

void clean_up_world_data(void);
void make_world_data(void);

void set_focus_for_world(void);
void remove_focus_for_world(void);

void hard_initialize_world(raw_input_t *raw_input, VkCommandPool *cmdpool, enum application_type_t app_type, enum application_mode_t app_mode);
void initialize_world(raw_input_t *raw_input, VkCommandPool *cmdpool, enum application_type_t type, enum application_mode_t mode);
void initialize_world(game_state_initialize_packet_t *packet, raw_input_t *raw_input);
void deinitialize_world(void);
void destroy_world(void);
void tick_world(raw_input_t *raw_input, float32_t dt, uint32_t image_index, uint32_t current_frame, gpu_command_queue_t *queue, enum application_type_t type, enum element_focus_t focus);
void handle_world_input(raw_input_t *raw_input, float32_t dt);
void handle_input_debug(raw_input_t *raw_input, float32_t dt);
void initialize_world_translation_unit(struct game_memory_t *memory);

uint64_t *get_current_tick(void);

int32_t convert_3d_to_1d_index(uint32_t x, uint32_t y, uint32_t z, uint32_t edge_length);
struct voxel_coordinate_t {uint8_t x, y, z;};
voxel_coordinate_t convert_1d_to_3d_coord(uint16_t index, uint32_t edge_length);
uint32_t get_chunk_grid_size(void);
