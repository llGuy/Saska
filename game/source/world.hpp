/* world.hpp */

#pragma once

#include "core.hpp"
#include "vulkan.hpp"
//#include "network.hpp"
#include "graphics.hpp"
#include <glm/glm.hpp>
#include <glm/gtx/transform.hpp>

#include "player.hpp"
#include "game_input.hpp"

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
void tick_world(game_input_t *game_input, float32_t dt, uint32_t image_index, uint32_t current_frame, gpu_command_queue_t *queue, enum application_type_t type, enum element_focus_t focus);
void handle_world_input(raw_input_t *raw_input, float32_t dt);
void handle_input_debug(raw_input_t *raw_input, float32_t dt);
void initialize_world_translation_unit(struct game_memory_t *memory);

uint64_t *get_current_tick(void);

int32_t convert_3d_to_1d_index(uint32_t x, uint32_t y, uint32_t z, uint32_t edge_length);
struct voxel_coordinate_t {uint8_t x, y, z;};
voxel_coordinate_t convert_1d_to_3d_coord(uint16_t index, uint32_t edge_length);
uint32_t get_chunk_grid_size(void);
