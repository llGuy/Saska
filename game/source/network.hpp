#pragma once

#include "utils.hpp"
#include "world.hpp"
#include <winsock2.h>
#include <ws2tcpip.h>
#define _WINSOCKAPI_ 
#include "thread_pool.hpp"

// Send, receive, etc...
// Network stuff, socket stuff, ...
struct network_socket_t
{
    int32_t socket;
};

struct network_address_t
{
    uint16_t port;
    uint32_t ipv4_address;
};

void add_network_socket(network_socket_t *socket);
void initialize_network_socket(network_socket_t *socket, int32_t family, int32_t type, int32_t protocol);
void bind_network_socket_to_port(network_socket_t *socket, network_address_t address);
void set_socket_to_non_blocking_mode(network_socket_t *socket);

int32_t receive_from(network_socket_t *socket, char *buffer, uint32_t buffer_size, network_address_t *address_dst);
bool send_to(network_socket_t *socket, network_address_t address, char *buffer, uint32_t buffer_size);

uint32_t str_to_ipv4_int32(const char *address);
uint32_t host_to_network_byte_order(uint32_t bytes);
uint32_t network_to_host_byte_order(uint32_t bytes);
float32_t host_to_network_byte_order_f32(float32_t bytes);
float32_t network_to_host_byte_order_f32(float32_t bytes);

void initialize_socket_api(void);

struct socket_manager_t
{
    static constexpr uint32_t MAX_SOCKETS = 50;
    SOCKET sockets[MAX_SOCKETS] = {};
    uint32_t socket_count = 0;
};

struct serializer_t
{
    uint32_t data_buffer_size;
    uint8_t *data_buffer;
    uint32_t data_buffer_head = 0;
};

void initialize_serializer(serializer_t *serializer, uint32_t max_size);
uint8_t *grow_serializer_data_buffer(serializer_t *serializer, uint32_t bytes);
void send_serialized_message(serializer_t *serializer, network_address_t address);
void receive_serialized_message(serializer_t *serializer, network_address_t address);

void serialize_uint8(serializer_t *serializer, uint8_t u8);
void serialize_bytes(serializer_t *serializer, uint8_t *bytes, uint32_t size);
void serialize_uint16(serializer_t *serializer, uint16_t u16);
void serialize_uint32(serializer_t *serializer, uint32_t u32);
void serialize_uint64(serializer_t *serializer, uint64_t u64);
void serialize_float32(serializer_t *serializer, float32_t f32);
void serialize_vector3(serializer_t *serializer, const vector3_t &v3);
void serialize_string(serializer_t *serializer, const char *string);

uint8_t deserialize_uint8(serializer_t *serializer);
uint16_t deserialize_uint16(serializer_t *serializer);
uint32_t deserialize_uint32(serializer_t *serializer);
uint64_t deserialize_uint64(serializer_t *serializer);
float32_t deserialize_float32(serializer_t *serializer);
vector3_t deserialize_vector3(serializer_t *serializer);
const char *deserialize_string(serializer_t *serializer);
void deserialize_bytes(serializer_t *serializer, uint8_t *bytes, uint32_t size);

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

void serialize_client_input_state_packet(serializer_t *serializer, player_state_t *packet);
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

    const uint16_t GAME_OUTPUT_PORT_SERVER = 6000;
    const uint16_t GAME_OUTPUT_PORT_CLIENT = 6001;
    const uint16_t GAME_OUTPUT_PORT_BACKUP = 6002;
    network_socket_t main_network_socket;
    // For client
    network_address_t server_address;

    // API stuff
    socket_manager_t sockets;

    uint32_t client_count = {};
    client_t clients[MAX_CLIENTS] = {};

    hash_table_inline_t<uint16_t /* Index into clients array */, MAX_CLIENTS * 2, 3, 3> client_table_by_name;
    hash_table_inline_t<uint16_t /* Index into clients array */, MAX_CLIENTS * 2, 3, 3> client_table_by_address;

    uint16_t client_id_stack[MAX_CLIENTS] = {};
    
    // Some sort of keep track of player's previous state (so that client can compare with the game snapshots that the server sent)
    bool fill_requested = 0;
    circular_buffer_t<player_state_t> player_state_history;

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
void buffer_player_state(float32_t dt);
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
