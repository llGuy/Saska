#pragma once

#include "component.hpp"
#include "player.hpp"
#include "sockets.hpp"
#include "utility.hpp"
#include "containers.hpp"


struct historical_player_state_t
{
    bool ackowledged = 0;
    uint64_t tick;
    player_state_t player_state;
};


// This will be received from the action flag packets (player state / command / update packets)
struct local_client_modified_voxel_t
{
    uint8_t x, y, z, value;
};


#define MAX_VOXELS_MODIFIED_PER_CHUNK 100

struct client_modified_chunk_nl_t
{
    uint16_t chunk_index;
    // TODO: Find way to vary this: maybe have its own linear allocator or something
    local_client_modified_voxel_t modified_voxels[MAX_VOXELS_MODIFIED_PER_CHUNK]; // max 100
    uint32_t modified_voxel_count;
};


struct client_t
{
    // Name, id, etc...
    const char *name;
    uint16_t client_id;
    
    network_address_t network_address;

    uint32_t network_component_index;

    uint64_t current_packet_count;

    // Handle to the player struct in the world_t megastruct
    player_handle_t player_handle;

    circular_buffer_t<historical_player_state_t> player_state_history;

    // Tick id of the previously received client input packet
    uint64_t previous_client_tick;
    player_state_t previous_received_player_state;
    
    // Client will most likely only ever be able to terraform 4 chunks per action flag packet
    uint32_t modified_chunks_count = 0;
    // Accumulates for every action flag packet
    // Check if 30 is necessary
    client_modified_chunk_nl_t previous_received_voxel_modifications[30];

    bool received_input_commands = 0;

    bool just_received_correction = 0;
    uint32_t last_received_correction_packet_count = 0;

    // Server will not take in input commands from client if this flag does not get set to false (gets set to false when server receives error correction packet)
    bool needs_to_acknowledge_prediction_error = 0;
    bool needs_to_do_voxel_correction = 0;
    bool did_voxel_correction = 0;
};


void initialize_client(char *message_buffer);
void tick_client(raw_input_t *raw_input, float32_t dt);
void cache_player_state(float32_t dt);
void send_prediction_error_correction(uint64_t tick);
client_t *get_user_client(void);
uint64_t &get_current_player_state_count(void);

void join_server(const char *ip_address, const char *client_name);