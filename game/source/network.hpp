#pragma once

#include "utils.hpp"
#include "world.hpp"
#include "thread_pool.hpp"
#include "sockets.hpp"
#include "serializer.hpp"
#include "client.hpp"


#define MAX_CLIENTS 40


constexpr uint16_t GAME_OUTPUT_PORT_CLIENT = 6001;
constexpr uint16_t GAME_OUTPUT_PORT_SERVER = 6000;


enum application_mode_t { CLIENT_MODE, SERVER_MODE };


#define MAX_RECEIVED_PACKETS_IN_QUEUE 60
struct receiver_thread_t
{
    linear_allocator_t packet_allocator = {};
    thread_process_t process;

    // Pointers into the packet allocator
    uint32_t packet_count;
    void *packets[MAX_RECEIVED_PACKETS_IN_QUEUE];
    uint32_t packet_sizes[MAX_RECEIVED_PACKETS_IN_QUEUE];
    network_address_t addresses[MAX_RECEIVED_PACKETS_IN_QUEUE];
    
    // Mutex
    mutex_t *mutex;

    uint32_t receiver_thread_loop_count = 0;

    bool receiver_freezed = 0;
};


struct network_state_t
{
    bool is_connected_to_server = false;
    
    application_mode_t current_app_mode;


    network_socket_t main_network_socket;
    // For client
    network_address_t server_address;

    uint32_t client_count = {};
    client_t clients[MAX_CLIENTS] = {};

    hash_table_inline_t<uint16_t /* Index into clients array */, MAX_CLIENTS * 2, 3, 3> client_table_by_name;
    hash_table_inline_t<uint16_t /* Index into clients array */, MAX_CLIENTS * 2, 3, 3> client_table_by_address;

    uint16_t client_id_stack[MAX_CLIENTS] = {};
    
    // Keeps track of player states to send to the server (like 25 a second or whatever value is specified)
    circular_buffer_t<player_state_t> player_state_cbuffer;

    // Settings:
    // Rate settings are all on a per second basis
    float32_t client_input_snapshot_rate = 25.0f;
    float32_t server_game_state_snapshot_rate = 20.0f;

    // This will be used for the server (maybe also for client in future, but for now, just the server)
    receiver_thread_t receiver_thread;


    // For debugging
    bool client_will_freeze_after_input = 0;
    bool sent_active_action_flags = 0;
};

void send_prediction_error_correction(uint64_t tick);

void fill_last_player_state_if_needed(player_t *player);

uint32_t add_client(network_address_t network_address, const char *client_name, player_handle_t player_handle);
client_t *get_client(uint32_t index);
void update_network_state(input_state_t *input_state, float32_t dt);

float32_t get_snapshot_server_rate(void);

void initialize_network_translation_unit(struct game_memory_t *memory);
void initialize_network_state(struct game_memory_t *memory, application_mode_t app_mode);



void join_server(const char *ip_address, const char *client_name);


// Helper stuff
constexpr uint32_t sizeof_packet_header(void) { return(sizeof(packet_header_t::bytes) +
                                                       sizeof(packet_header_t::current_tick) +
                                                       sizeof(packet_header_t::client_id)); }
constexpr uint32_t sizeof_client_input_state_packet(void) { return(sizeof(client_input_state_packet_t::action_flags) +
                                                                   sizeof(client_input_state_packet_t::mouse_x_diff) +
                                                                   sizeof(client_input_state_packet_t::mouse_y_diff) +
                                                                   sizeof(client_input_state_packet_t::flags_byte) +
                                                                   sizeof(client_input_state_packet_t::dt)); }
constexpr uint32_t sizeof_game_snapshot_player_state_packet(void) { return(sizeof(game_snapshot_player_state_packet_t::client_id) +
                                                                           sizeof(game_snapshot_player_state_packet_t::ws_position) +
                                                                           sizeof(game_snapshot_player_state_packet_t::ws_direction) +
                                                                           sizeof(game_snapshot_player_state_packet_t::ws_velocity) +
                                                                           sizeof(game_snapshot_player_state_packet_t::ws_up_vector) +
                                                                           sizeof(game_snapshot_player_state_packet_t::ws_rotation) +
                                                                           sizeof(game_snapshot_player_state_packet_t::action_flags) +
                                                                           sizeof(game_snapshot_player_state_packet_t::flags)); }
constexpr uint32_t sizeof_modified_voxel(void) { return(sizeof(modified_voxel_t::previous_value) +
                                                        sizeof(modified_voxel_t::next_value) +
                                                        sizeof(modified_voxel_t::index)); };
inline uint32_t sizeof_modified_chunk(uint32_t modified_chunk_count) { return(sizeof(modified_chunk_t::chunk_index) +
                                                                       sizeof(modified_chunk_t::modified_voxel_count) +
                                                                       sizeof_modified_voxel() * modified_chunk_count); };
inline uint32_t sizeof_game_snapshot_voxel_delta_packet(uint32_t modified_chunk_count, modified_chunk_t *chunks)
{
    uint32_t size = sizeof(game_snapshot_voxel_delta_packet_t::modified_count);
    for (uint32_t chunk = 0; chunk < modified_chunk_count; ++chunk)
    {
        size += sizeof_modified_chunk(chunks[chunk].modified_voxel_count);
    }
    return(size);
}


constexpr uint32_t sizeof_local_client_modified_voxel(void) { return(sizeof(local_client_modified_voxel_t::x) +
                                                                     sizeof(local_client_modified_voxel_t::y) +
                                                                     sizeof(local_client_modified_voxel_t::z) +
                                                                     sizeof(local_client_modified_voxel_t::value)); };
inline uint32_t sizeof_client_modified_chunk(uint32_t modified_chunk_count) { return(sizeof(client_modified_chunk_t::chunk_index) +
                                                                                     sizeof(client_modified_chunk_t::modified_voxel_count) +
                                                                                     sizeof_modified_voxel() * modified_chunk_count); };
inline uint32_t sizeof_modified_voxels_packet(uint32_t modified_chunk_count, client_modified_chunk_t *chunks)
{
    uint32_t size = sizeof(client_modified_voxels_packet_t::modified_chunk_count);
    for (uint32_t chunk = 0; chunk < modified_chunk_count; ++chunk)
    {
        size += sizeof_modified_chunk(chunks[chunk].modified_voxel_count);
    }
    return(size);
}
