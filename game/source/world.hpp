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




struct dbg_entities_t
{
    bool hit_box_display = false;
    player_t *render_sliding_vector_player = nullptr;
};



struct world_t
{
    // Not hard initialize (rendering state, vulkan objects, shaders...) but just initialize game data like initialized entities, voxels, etc...
    bool initialized_world;


    // Gets incremented every frame
    uint64_t current_tick_id = 0;
};

// Gets the data of the player that is being controlled by client

player_handle_t initialize_player_from_player_init_packet(uint32_t local_user_client_index, struct player_state_initialize_packet_t *player_init_packet, camera_handle_t camera /* Optional */ = 0);
//player_handle_t spawn_player_at(const char *player_name, player_color_t color, const vector3_t &ws_position, const quaternion_t &quat);
void make_player_renderable(player_handle_t player_handle, player_color_t color);
void make_player_main(player_handle_t player_handle, raw_input_t *raw_input);

void update_network_world_state(void);
void update_networked_player(uint32_t player_index);
void initialize_game_state_initialize_packet(struct game_state_initialize_packet_t *packet, player_handle_t new_client_handle);
// Will create packet for each chunk
struct voxel_chunk_values_packet_t *initialize_chunk_values_packets(uint32_t *count);
void clear_chunk_history_for_server(void);

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
voxel_coordinate_t convert_1d_to_3d_coord(uint16_t index, uint32_t edge_length);
