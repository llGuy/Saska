// TODO: Reduce public interface

#pragma once


#include "chunk.hpp"
#include "utility.hpp"


struct chunks_state_flags_t
{
    // Should not be updating if 
    uint32_t should_update_chunk_meshes_from_now: 1;
    // Number of chunks to update received from server
    uint32_t chunks_received_to_update_count: 7;
    // Number that the client is waiting for
    uint32_t chunks_to_be_received: 8;
};

// Happens once during lifetime of the program (just initializes rendering data, permanent stuff, ...)
void initialize_chunks_state(void);
// Happens whenever voxels need to be filled (when client joins server, loads map, launches editor, ...)
void populate_chunks_state(struct game_state_initialize_packet_t *packet);
void deinitialize_chunks_state(void);

void fill_game_state_initialize_packet_with_chunk_state(struct game_state_initialize_packet_t *packet);
struct voxel_chunk_values_packet_t *initialize_chunk_values_packets(uint32_t *count);

void ray_cast_terraform(const vector3_t &ws_position, const vector3_t &ws_direction, float32_t max_reach_distance, float32_t dt, uint32_t surface_level, bool destructive, float32_t speed);

void tick_chunks_state(float32_t dt);

void render_chunks_to_shadowmap(uniform_group_t *transforms, gpu_command_queue_t *queue);
void render_chunks(uniform_group_t *uniforms, gpu_command_queue_t *queue);
void sync_gpu_with_chunks_state(struct gpu_command_queue_t *queue);

void reset_voxel_interpolation(void);
void ready_chunk_for_gpu_sync(chunk_t *chunk);
void clear_chunk_history(void);


// TODO: See if this needs to be removed
void terraform_client(const ivector3_t &xs_voxel_coord, uint32_t voxel_radius, bool destructive, float32_t dt, float32_t speed);


vector3_t ws_to_xs(const vector3_t &ws_position);

// Some stuff that other modules may need access to
chunk_t **get_chunk(int32_t index);
chunk_t **get_chunk(uint32_t x, uint32_t y, uint32_t z);

chunks_state_flags_t *get_chunks_state_flags(void);

chunk_t *get_chunk_encompassing_point(const vector3_t &xs_position);

ivector3_t get_voxel_coord(const vector3_t &xs_position);
ivector3_t get_voxel_coord(const ivector3_t &xs_position);

struct linear_allocator_t *get_voxel_linear_allocator(void);

struct game_snapshot_voxel_delta_packet_t *&get_previous_voxel_delta_packet(void);

chunk_t **get_modified_chunks(uint32_t *count);
float32_t get_chunk_grid_size(void);
float32_t get_chunk_size(void);