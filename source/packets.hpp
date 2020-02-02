#pragma once

#include "utility.hpp"
#include "client.hpp"

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
    uint64_t current_packet_id;
    // When client sends to server, this needs to be filled
    uint32_t client_id;
};


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
            uint8_t physics_state: 2;
        } flags;
        uint8_t flags_byte;
    };
    float32_t dt;

    // To remove when not testing
    uint64_t command_id;
};



struct client_modified_chunk_t
{
    uint16_t chunk_index;
    uint32_t modified_voxel_count;
    struct local_client_modified_voxel_t *modified_voxels;
};

struct client_modified_voxels_packet_t
{
    uint32_t modified_chunk_count = 0;
    client_modified_chunk_t *modified_chunks;
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
    vector3_t ws_previous_velocity;
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
            uint8_t physics_state : 2;
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



constexpr uint32_t sizeof_packet_header(void) 
{ 
    return(sizeof(packet_header_t::bytes) +
        sizeof(packet_header_t::current_tick) +
        sizeof(packet_header_t::current_packet_id) +
        sizeof(packet_header_t::client_id));
}

constexpr uint32_t sizeof_client_input_state_packet(void)
{
    return(sizeof(client_input_state_packet_t::action_flags) +
        sizeof(client_input_state_packet_t::mouse_x_diff) +
        sizeof(client_input_state_packet_t::mouse_y_diff) +
        sizeof(client_input_state_packet_t::flags_byte) +
        sizeof(client_input_state_packet_t::dt) +
        sizeof(client_input_state_packet_t::command_id));
};

constexpr uint32_t sizeof_game_snapshot_player_state_packet(void) { return(sizeof(game_snapshot_player_state_packet_t::client_id) +
                                                                           sizeof(game_snapshot_player_state_packet_t::ws_position) +
                                                                           sizeof(game_snapshot_player_state_packet_t::ws_direction) +
                                                                           sizeof(game_snapshot_player_state_packet_t::ws_velocity) +
                                                                           sizeof(game_snapshot_player_state_packet_t::ws_previous_velocity) +
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
