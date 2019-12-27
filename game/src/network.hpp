#pragma once

#include "utils.hpp"
#include "world.hpp"
#include "raw_input.hpp"
#include "thread_pool.hpp"
#include "network_serializer.hpp"


// Game network code
enum application_mode_t { CLIENT_MODE, SERVER_MODE };


void initialize_network_state(application_mode_t app_mode);


enum packet_mode_t { PM_CLIENT_MODE, PM_SERVER_MODE };
enum client_packet_type_t { CPT_CLIENT_JOIN, CPT_INPUT_STATE, CPT_ACKNOWLEDGED_GAME_STATE_RECEPTION, CPT_PREDICTION_ERROR_CORRECTION };
enum server_packet_type_t { SPT_SERVER_HANDSHAKE, SPT_CHUNK_VOXELS_HARD_UPDATE, SPT_GAME_STATE_SNAPSHOT, SPT_CLIENT_JOINED };


struct packet_header_t
{
    union
    {
        struct
        {
            uint32_t packet_mode: 1;
            uint32_t packet_type: 4 /* To increase in the future when more packet types appear */;
            // Includes header
            uint32_t total_packet_size: 27;
        };

        uint32_t bytes;
    };

    // To make sure order is still all good
    uint64_t current_tick;
    // When client sends to server, this needs to be filled
    uint32_t client_id;
};


void serialize_packet_header(serializer_t *serializer, packet_header_t *packet);
void deserialize_packet_header(serializer_t *serializer, packet_header_t *packet);


// This also needs to send the state of the world
struct server_handshake_packet_t
{
    uint16_t client_id;
    uint8_t color;
};


#define CLIENT_NAME_MAX_LENGTH 40


struct client_join_packet_t
{
    const char *client_name;
};


struct voxel_state_initialize_packet_t
{
    uint32_t grid_edge_size;
    float32_t size;
    uint32_t chunk_count;
    uint32_t max_chunks;
};


// TODO: This needs to contain more stuff
struct player_state_initialize_packet_t
{
    uint32_t client_id;
    const char *player_name;

    union
    {
        struct
        {
            float32_t ws_position_x;
            float32_t ws_position_y;
            float32_t ws_position_z;
        };
        vector3_t ws_position;
    };

    union
    {
        struct
        {
            float32_t ws_view_direction_x;
            float32_t ws_view_direction_y;
            float32_t ws_view_direction_z;
        };
        vector3_t ws_direction;
    };
};


struct voxel_chunk_values_packet_t
{
    uint8_t chunk_coord_x;
    uint8_t chunk_coord_y;
    uint8_t chunk_coord_z;
    uint8_t *voxels;
};


struct voxel_chunks_hard_update_packet_t
{
    uint32_t to_update_chunks;
    voxel_chunk_values_packet_t *packets;
};


struct game_state_initialize_packet_t
{
    voxel_state_initialize_packet_t voxels;
    uint32_t client_index;
    uint32_t player_count;
    player_state_initialize_packet_t *player;
};


// Command packet
struct client_input_state_packet_t
{
    // Contains stuff like which buttons did the client press
    // Server will then use this input to update the client's data on the server side
    // Server will then send back some snippets of the actual state of the game while the client is "guessing" what the current state is
    uint32_t action_flags;
    float32_t mouse_x_diff;
    float32_t mouse_y_diff;
    union
    {
        struct
        {
            uint8_t is_entering: 1;
            uint8_t rolling_mode: 1;
        } flags;
        uint8_t flags_byte;
    };
    float32_t dt;
};


// This will be received from the action flag packets (player state / command / update packets)
struct local_client_modified_voxel_t
{
    uint8_t x, y, z, value;
};


struct client_modified_chunk_t
{
    uint16_t chunk_index;
    uint32_t modified_voxel_count;
    local_client_modified_voxel_t *modified_voxels;
};


struct client_modified_voxels_packet_t
{
    uint32_t modified_chunk_count = 0;
    client_modified_chunk_t *modified_chunks;
};


struct client_modified_chunk_nl_t
{
    uint16_t chunk_index;
    // TODO: Find way to vary this: maybe have its own linear allocator or something
    local_client_modified_voxel_t modified_voxels[80]; // max 40
    uint32_t modified_voxel_count;
};


struct game_state_acknowledge_packet_t
{
    uint64_t game_state_tick;
};


// State that the client will use to simply verify if the client prediction hasn't been incorrect
struct player_state_to_verify_t
{
    vector3_t ws_position;
    vector3_t ws_direction;
};


// This gets sent to the client at intervals (snapshots) - 20 per second
// TODO: This needs to contain more stuff (like velocity)
struct game_snapshot_player_state_packet_t
{
    uint16_t client_id;
    vector3_t ws_position;
    vector3_t ws_direction;
    vector3_t ws_velocity;
    vector3_t ws_up_vector;
    quaternion_t ws_rotation;
    // Just fill these in so that the clients can interpolate between different action animations
    uint32_t action_flags;

    union
    {
        struct
        {
            uint8_t need_to_do_correction: 1;
            uint8_t need_to_do_voxel_correction: 1;
            uint8_t is_to_ignore: 1;
            uint8_t is_rolling: 1;
        };
        uint8_t flags;
    };
};


struct modified_voxel_t
{
    uint8_t previous_value, next_value;
    uint16_t index;
};


struct modified_chunk_t
{
    uint16_t chunk_index;
    modified_voxel_t *modified_voxels;
    uint32_t modified_voxel_count;
};


struct game_snapshot_voxel_delta_packet_t
{
    uint32_t modified_count;
    modified_chunk_t *modified_chunks;
};


struct client_prediction_error_correction_t
{
    uint64_t tick;
};


void serialize_player_state_initialize_packet(serializer_t *serializer, player_state_initialize_packet_t *packet);
void deserialize_player_state_initialize_packet(serializer_t *serializer, player_state_initialize_packet_t *packet);
void serialize_voxel_state_initialize_packet(serializer_t *serializer, voxel_state_initialize_packet_t *packet);
void deserialize_voxel_state_initialize_packet(serializer_t *serializer, voxel_state_initialize_packet_t *packet);
void serialize_voxel_chunk_values_packet(serializer_t *serializer, voxel_chunk_values_packet_t *packet);
void deserialize_voxel_chunk_values_packet(serializer_t *serializer, voxel_chunk_values_packet_t *packet);

void serialize_game_state_initialize_packet(serializer_t *serializer, game_state_initialize_packet_t *packet);
void deserialize_game_state_initialize_packet(serializer_t *serializer, game_state_initialize_packet_t *packet);
void serialize_client_join_packet(serializer_t *serializer, client_join_packet_t *packet);
void deserialize_client_join_packet(serializer_t *serializer, client_join_packet_t *packet);
void serialize_client_input_state_packet(serializer_t *serializer, struct player_state_t *packet);
void deserialize_client_input_state_packet(serializer_t *serializer, client_input_state_packet_t *packet);
void serialize_game_snapshot_player_state_packet(serializer_t *serializer, game_snapshot_player_state_packet_t *packet);
void deserialize_game_snapshot_player_state_packet(serializer_t *serializer, game_snapshot_player_state_packet_t *packet);


struct historical_player_state_t
{
    bool ackowledged = 0;
    uint64_t tick;
    player_state_t player_state;
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
    client_modified_chunk_nl_t previous_received_voxel_modifications[30];

    bool received_input_commands = 0;



    // Server will not take in input commands from client if this flag does not get set to false (gets set to false when server receives error correction packet)
    bool needs_to_acknowledge_prediction_error = 0;
    bool needs_to_do_voxel_correction = 0;
    bool did_voxel_correction = 0;
};


// Function that the client application is going to use when it comes to interpolating between voxel values
client_modified_chunk_nl_t *previous_client_modified_chunks(uint32_t *count);


#define MAX_CLIENTS 40





void send_prediction_error_correction(uint64_t tick);

uint32_t add_client(network_address_t network_address, const char *client_name, player_handle_t player_handle);
client_t *get_client(uint32_t index);
void buffer_player_state(float32_t dt);
void update_network_state(raw_input_t *raw_input, float32_t dt);


float32_t get_snapshot_server_rate(void);


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
