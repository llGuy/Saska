// BUG: WHY DO OTHER PLAYERS ROTATE WEIRDLY WHEN CLIENT MOVES?!??????! WTF

#include <cassert>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <Windows.h>
#include "network.hpp"
#include "game.hpp"
#include "ui.hpp"
#include "script.hpp"
#include "core.hpp"
#include "thread_pool.hpp"

// Network code stuff for windows
global_var network_state_t *g_network_state;

void add_network_socket(network_socket_t *socket)
{
    socket->socket = g_network_state->sockets.socket_count++;
}

SOCKET *get_network_socket(network_socket_t *socket)
{
    return(&g_network_state->sockets.sockets[socket->socket]);
}

void initialize_network_socket(network_socket_t *socket_p, int32_t family, int32_t type, int32_t protocol)
{
    SOCKET new_socket = socket(family, type, protocol);

    if (new_socket == INVALID_SOCKET)
    {
        OutputDebugString("Failed to initialize socket\n");
        assert(0);
    }
    g_network_state->sockets.sockets[socket_p->socket] = new_socket;
}

void bind_network_socket_to_port(network_socket_t *socket, network_address_t address)
{
    SOCKET *sock = get_network_socket(socket);
    
    // Convert network_address_t to SOCKADDR_IN
    SOCKADDR_IN address_struct {};
    address_struct.sin_family = AF_INET;
    // Needs to be in network byte order
    address_struct.sin_port = address.port;
    address_struct.sin_addr.s_addr = INADDR_ANY;
    
    if (bind(*sock, (SOCKADDR *)&address_struct, sizeof(address_struct)) == SOCKET_ERROR)
    {
        OutputDebugString("Failed to bind socket to local port");
        
        // Try again with different port
        ++address.port;
        bind_network_socket_to_port(socket, address);
    }
}

void set_socket_to_non_blocking_mode(network_socket_t *socket)
{
    SOCKET *sock = get_network_socket(socket);
    u_long enabled = 1;
    ioctlsocket(*sock, FIONBIO, &enabled);
}

int32_t receive_from(network_socket_t *socket, char *buffer, uint32_t buffer_size, network_address_t *address_dst)
{
    SOCKET *sock = get_network_socket(socket);

    SOCKADDR_IN from_address = {};
    int32_t from_size = sizeof(from_address);
    
    int32_t bytes_received = recvfrom(*sock, buffer, buffer_size, 0, (SOCKADDR *)&from_address, &from_size);

    if (bytes_received == SOCKET_ERROR)
    {
        return 0;
    }
    else
    {
        buffer[bytes_received] = 0;
    }

    network_address_t received_address = {};
    received_address.port = from_address.sin_port;
    received_address.ipv4_address = from_address.sin_addr.S_un.S_addr;

    *address_dst = received_address;
    
    return(bytes_received);
}

bool send_to(network_socket_t *socket, network_address_t address, char *buffer, uint32_t buffer_size)
{
    SOCKET *sock = get_network_socket(socket);
    
    SOCKADDR_IN address_struct = {};
    address_struct.sin_family = AF_INET;
    // Needs to be in network byte order
    address_struct.sin_port = address.port;
    address_struct.sin_addr.S_un.S_addr = address.ipv4_address;
    
    int32_t address_size = sizeof(address_struct);

    int32_t sendto_ret = sendto(*sock, buffer, buffer_size, 0, (SOCKADDR *)&address_struct, sizeof(address_struct));

    if (sendto_ret == SOCKET_ERROR)
    {
        char error_n[32];
        sprintf(error_n, "sendto failed: %d\n", WSAGetLastError());
        OutputDebugString(error_n);
        assert(0);
    }

    return(sendto_ret != SOCKET_ERROR);
}

uint32_t str_to_ipv4_int32(const char *address)
{
    return(inet_addr(address));
}

uint32_t host_to_network_byte_order(uint32_t bytes)
{
    return(htons(bytes));
}

uint32_t network_to_host_byte_order(uint32_t bytes)
{
    return(ntohl(bytes));
}

void initialize_socket_api(void)
{
    // This is only for Windows, make sure to change if using Linux / Mac
    WSADATA winsock_data;
    if (WSAStartup(0x202, &winsock_data))
    {
        OutputDebugString("Failed to initialize Winsock\n");
        assert(0);
    }
}

void initialize_serializer(serializer_t *serializer, uint32_t max_size)
{
    serializer->data_buffer = (uint8_t *)allocate_linear(max_size * sizeof(uint8_t));
}

uint8_t *grow_serializer_data_buffer(serializer_t *serializer, uint32_t bytes)
{
    uint32_t previous = serializer->data_buffer_head;
    serializer->data_buffer_head += bytes;
    return(&serializer->data_buffer[previous]);
}

void send_serialized_message(serializer_t *serializer, network_address_t address)
{
    send_to(&g_network_state->main_network_socket,
            address,
            (char *)serializer->data_buffer,
            serializer->data_buffer_head);
}

void serialize_uint8(serializer_t *serializer, uint8_t u8)
{
    uint8_t *pointer = grow_serializer_data_buffer(serializer, 1);
    *pointer = u8;
}

uint8_t deserialize_uint8(serializer_t *serializer)
{
    uint8_t *pointer = grow_serializer_data_buffer(serializer, 1);
    return(*pointer);
}

void serialize_float32(serializer_t *serializer, float32_t f32)
{
    uint8_t *pointer = grow_serializer_data_buffer(serializer, 4);
#if defined (__i386) || defined (__x86_64__) || defined (_M_IX86) || defined(_M_X64)
    *(float32_t *)pointer = f32;
#else
    uint32_t *f = (uint32_t *)&f32;
    *pointer++ = (uint8_t)*f;
    *pointer++ = (uint8_t)(*f >> 8);
    *pointer++ = (uint8_t)(*f >> 16);
    *pointer++ = (uint8_t)(*f >> 24);
#endif
}

float32_t deserialize_float32(serializer_t *serializer)
{
    uint8_t *pointer = grow_serializer_data_buffer(serializer, 4);
#if defined (__i386) || defined (__x86_64__) || defined (_M_IX86) || defined(_M_X64)
    return(*(float32_t *)pointer);
#else
    uint32_t ret = 0;
    ret += (*pointer++);
    ret += ((uint32_t)(*pointer++)) << 8;
    ret += ((uint32_t)(*pointer++)) << 16;
    ret += ((uint32_t)(*pointer++)) << 24;
    
    return(*(float32_t *)(&ret));
#endif
}

void serialize_vector3(serializer_t *serializer, const vector3_t &v3)
{
    serialize_float32(serializer, v3.x);
    serialize_float32(serializer, v3.y);
    serialize_float32(serializer, v3.z);
}

vector3_t deserialize_vector3(serializer_t *serializer)
{
    vector3_t v3 = {};
    v3.x = deserialize_float32(serializer);
    v3.y = deserialize_float32(serializer);
    v3.z = deserialize_float32(serializer);

    return(v3);
}

void serialize_uint16(serializer_t *serializer, uint16_t u16)
{
    uint8_t *pointer = grow_serializer_data_buffer(serializer, 2);
#if defined (__i386) || defined (__x86_64__) || defined (_M_IX86) || defined(_M_X64)
    *(uint16_t *)pointer = u16;
#else
    *pointer++ = (uint8_t)u16;
    *pointer++ = (uint8_t)(u16 >> 8);
#endif
}

uint16_t deserialize_uint16(serializer_t *serializer)
{
    uint8_t *pointer = grow_serializer_data_buffer(serializer, 2);
#if defined (__i386) || defined (__x86_64__) || defined (_M_IX86) || defined(_M_X64)
    return(*(uint16_t *)pointer);
#else
    uint16_t ret = 0;
    ret += (*pointer++);
    ret += ((uint16_t)(*pointer++)) << 8;
    return(ret);
#endif
}

void serialize_uint32(serializer_t *serializer, uint32_t u32)
{
    uint8_t *pointer = grow_serializer_data_buffer(serializer, 4);
#if defined (__i386) || defined (__x86_64__) || defined (_M_IX86) || defined(_M_X64)
    *(uint32_t *)pointer = u32;
#else
    *pointer++ = (uint8_t)u32;
    *pointer++ = (uint8_t)(u32 >> 8);
    *pointer++ = (uint8_t)(u32 >> 16);
    *pointer++ = (uint8_t)(u32 >> 24);
#endif
}

uint32_t deserialize_uint32(serializer_t *serializer)
{
    uint8_t *pointer = grow_serializer_data_buffer(serializer, 4);
#if defined (__i386) || defined (__x86_64__) || defined (_M_IX86) || defined(_M_X64)
    return(*(uint32_t *)pointer);
#else
    uint32_t ret = 0;
    ret += (*pointer++);
    ret += ((uint32_t)(*pointer++)) << 8;
    ret += ((uint32_t)(*pointer++)) << 16;
    ret += ((uint32_t)(*pointer++)) << 24;
    return(ret);
#endif
}

void serialize_uint64(serializer_t *serializer, uint64_t u64)
{
    uint8_t *pointer = grow_serializer_data_buffer(serializer, 8);
#if defined (__i386) || defined (__x86_64__) || defined (_M_IX86) || defined(_M_X64)
    *(uint64_t *)pointer = u64;
#else
    *pointer++ = (uint8_t)u32;
    *pointer++ = (uint8_t)(u32 >> 8);
    *pointer++ = (uint8_t)(u32 >> 16);
    *pointer++ = (uint8_t)(u32 >> 24);
    *pointer++ = (uint8_t)(u32 >> 32);
    *pointer++ = (uint8_t)(u32 >> 40);
    *pointer++ = (uint8_t)(u32 >> 48);
    *pointer++ = (uint8_t)(u32 >> 56);
#endif
}

uint64_t deserialize_uint64(serializer_t *serializer)
{
    uint8_t *pointer = grow_serializer_data_buffer(serializer, 8);
#if defined (__i386) || defined (__x86_64__) || defined (_M_IX86) || defined(_M_X64)
    return(*(uint64_t *)pointer);
#else
    uint64_t ret = 0;
    ret += (*pointer++);
    ret += ((uint64_t)(*pointer++)) << 8;
    ret += ((uint64_t)(*pointer++)) << 16;
    ret += ((uint64_t)(*pointer++)) << 24;
    ret += ((uint64_t)(*pointer++)) << 32;
    ret += ((uint64_t)(*pointer++)) << 40;
    ret += ((uint64_t)(*pointer++)) << 48;
    ret += ((uint64_t)(*pointer++)) << 56;
    return(ret);
#endif
}

void serialize_string(serializer_t *serializer, const char *string)
{
    uint32_t string_length = strlen(string);
    
    uint8_t *pointer = grow_serializer_data_buffer(serializer, strlen(string) + 1);
    memcpy(pointer, string, string_length + 1);
}


const char *deserialize_string(serializer_t *serializer)
{
    uint8_t *pointer = &serializer->data_buffer[serializer->data_buffer_head];
    uint32_t string_length = strlen((char *)pointer);
    grow_serializer_data_buffer(serializer, string_length + 1);

    char *ret = (char *)allocate_free_list(string_length + 1);
    memcpy(ret, pointer, string_length + 1);
    return(ret);
}

// Hello there
void serialize_bytes(serializer_t *serializer, uint8_t *bytes, uint32_t size)
{
    uint8_t *pointer = grow_serializer_data_buffer(serializer, size);
    memcpy(pointer, bytes, size);
}


void deserialize_bytes(serializer_t *serializer, uint8_t *bytes, uint32_t size)
{
    uint8_t *pointer = grow_serializer_data_buffer(serializer, size);
    memcpy(bytes, pointer, size);
}


void serialize_packet_header(serializer_t *serializer, packet_header_t *packet)
{
    serialize_uint32(serializer, packet->bytes);
    serialize_uint64(serializer, packet->current_tick);
    serialize_uint32(serializer, packet->client_id);
}


void deserialize_packet_header(serializer_t *serializer, packet_header_t *packet)
{
    packet->bytes = deserialize_uint32(serializer);
    packet->current_tick = deserialize_uint64(serializer);
    packet->client_id = deserialize_uint32(serializer);
}


void serialize_client_join_packet(serializer_t *serializer, client_join_packet_t *packet)
{
    serialize_string(serializer, packet->client_name);
}


void deserialize_client_join_packet(serializer_t *serializer, client_join_packet_t *packet)
{
    packet->client_name = deserialize_string(serializer);
}


void serialize_client_input_state_packet(serializer_t *serializer, player_state_t *state)
{
    serialize_uint32(serializer, state->action_flags);
    serialize_float32(serializer, state->mouse_x_diff);
    serialize_float32(serializer, state->mouse_y_diff);
    serialize_uint8(serializer, state->flags_byte);
    serialize_float32(serializer, state->dt);
}


void deserialize_client_input_state_packet(serializer_t *serializer, client_input_state_packet_t *packet)
{
    packet->action_flags = deserialize_uint32(serializer);
    packet->mouse_x_diff = deserialize_float32(serializer);
    packet->mouse_y_diff = deserialize_float32(serializer);
    packet->flags_byte = deserialize_uint8(serializer);
    packet->dt = deserialize_float32(serializer);
}


void serialize_player_state_initialize_packet(serializer_t *serializer, player_state_initialize_packet_t *packet)
{
    serialize_uint32(serializer, packet->client_id);
    serialize_string(serializer, packet->player_name);
    
    serialize_float32(serializer, packet->ws_position_x);
    serialize_float32(serializer, packet->ws_position_y);
    serialize_float32(serializer, packet->ws_position_z);

    serialize_float32(serializer, packet->ws_view_direction_x);
    serialize_float32(serializer, packet->ws_view_direction_y);
    serialize_float32(serializer, packet->ws_view_direction_z);

    // Etc...
}


void deserialize_player_state_initialize_packet(serializer_t *serializer, player_state_initialize_packet_t *packet)
{
    packet->client_id = deserialize_uint32(serializer);
    packet->player_name = deserialize_string(serializer);
    
    packet->ws_position_x = deserialize_float32(serializer);
    packet->ws_position_y = deserialize_float32(serializer);
    packet->ws_position_z = deserialize_float32(serializer);

    packet->ws_view_direction_x = deserialize_float32(serializer);
    packet->ws_view_direction_y = deserialize_float32(serializer);
    packet->ws_view_direction_z = deserialize_float32(serializer);
    // Etc...
}


void serialize_voxel_state_initialize_packet(serializer_t *serializer, voxel_state_initialize_packet_t *packet)
{
    serialize_uint32(serializer, packet->grid_edge_size);
    serialize_float32(serializer, packet->size);
    serialize_uint32(serializer, packet->chunk_count);
    serialize_uint32(serializer, packet->max_chunks);
}


void deserialize_voxel_state_initialize_packet(serializer_t *serializer, voxel_state_initialize_packet_t *packet)
{
    packet->grid_edge_size = deserialize_uint32(serializer);
    packet->size = deserialize_float32(serializer);
    packet->chunk_count = deserialize_uint32(serializer);
    packet->max_chunks = deserialize_uint32(serializer);
}


void serialize_voxel_chunk_values_packet(serializer_t *serializer, voxel_chunk_values_packet_t *packet)
{
    serialize_uint8(serializer, packet->chunk_coord_x);
    serialize_uint8(serializer, packet->chunk_coord_y);
    serialize_uint8(serializer, packet->chunk_coord_z);
    serialize_bytes(serializer, packet->voxels, sizeof(uint8_t) * VOXEL_CHUNK_EDGE_LENGTH * VOXEL_CHUNK_EDGE_LENGTH * VOXEL_CHUNK_EDGE_LENGTH);
}


void deserialize_voxel_chunk_values_packet(serializer_t *serializer, voxel_chunk_values_packet_t *packet)
{
    packet->chunk_coord_x = deserialize_uint8(serializer);
    packet->chunk_coord_y = deserialize_uint8(serializer);
    packet->chunk_coord_z = deserialize_uint8(serializer);

    packet->voxels = (uint8_t *)allocate_linear(sizeof(uint8_t) * VOXEL_CHUNK_EDGE_LENGTH * VOXEL_CHUNK_EDGE_LENGTH * VOXEL_CHUNK_EDGE_LENGTH);
    deserialize_bytes(serializer, packet->voxels, sizeof(uint8_t) * VOXEL_CHUNK_EDGE_LENGTH * VOXEL_CHUNK_EDGE_LENGTH * VOXEL_CHUNK_EDGE_LENGTH);
}


void serialize_game_state_initialize_packet(serializer_t *serializer, game_state_initialize_packet_t *packet)
{
    serialize_voxel_state_initialize_packet(serializer, &packet->voxels);
    serialize_uint32(serializer, packet->client_index);
    serialize_uint32(serializer, packet->player_count);
    for (uint32_t i = 0; i < packet->player_count; ++i)
    {
        serialize_player_state_initialize_packet(serializer, &packet->player[i]);
    }
}


void serialize_game_snapshot_player_state_packet(serializer_t *serializer, game_snapshot_player_state_packet_t *packet)
{
    serialize_uint32(serializer, packet->client_id);
    
    serialize_float32(serializer, packet->ws_position.x);
    serialize_float32(serializer, packet->ws_position.y);
    serialize_float32(serializer, packet->ws_position.z);

    serialize_float32(serializer, packet->ws_direction.x);
    serialize_float32(serializer, packet->ws_direction.y);
    serialize_float32(serializer, packet->ws_direction.z);

    serialize_float32(serializer, packet->ws_velocity.x);
    serialize_float32(serializer, packet->ws_velocity.y);
    serialize_float32(serializer, packet->ws_velocity.z);

    serialize_float32(serializer, packet->ws_rotation[0]);
    serialize_float32(serializer, packet->ws_rotation[1]);
    serialize_float32(serializer, packet->ws_rotation[2]);
    serialize_float32(serializer, packet->ws_rotation[3]);

    serialize_uint8(serializer, packet->flags);
}


void deserialize_game_snapshot_player_state_packet(serializer_t *serializer, game_snapshot_player_state_packet_t *packet)
{
    packet->client_id = deserialize_uint32(serializer);
    
    packet->ws_position.x = deserialize_float32(serializer);
    packet->ws_position.y = deserialize_float32(serializer);
    packet->ws_position.z = deserialize_float32(serializer);

    packet->ws_direction.x = deserialize_float32(serializer);
    packet->ws_direction.y = deserialize_float32(serializer);
    packet->ws_direction.z = deserialize_float32(serializer);

    packet->ws_velocity.x = deserialize_float32(serializer);
    packet->ws_velocity.y = deserialize_float32(serializer);
    packet->ws_velocity.z = deserialize_float32(serializer);

    packet->ws_rotation[0] = deserialize_float32(serializer);
    packet->ws_rotation[1] = deserialize_float32(serializer);
    packet->ws_rotation[2] = deserialize_float32(serializer);
    packet->ws_rotation[3] = deserialize_float32(serializer);

    packet->flags = deserialize_uint8(serializer);
}


void deserialize_game_state_initialize_packet(serializer_t *serializer, game_state_initialize_packet_t *packet)
{
    deserialize_voxel_state_initialize_packet(serializer, &packet->voxels);
    packet->client_index = deserialize_uint32(serializer);
    packet->player_count = deserialize_uint32(serializer);
    packet->player = (player_state_initialize_packet_t *)allocate_linear(sizeof(player_state_initialize_packet_t) * packet->player_count);
    for (uint32_t i = 0; i < packet->player_count; ++i)
    {
        deserialize_player_state_initialize_packet(serializer, &packet->player[i]);
    }
}


void join_server(const char *ip_address, const char *client_name)
{
    packet_header_t header = {};
    client_join_packet_t packet = {};
    {
        header.packet_mode = packet_mode_t::PM_CLIENT_MODE;
        header.packet_type = client_packet_type_t::CPT_CLIENT_JOIN;
        packet.client_name = client_name;
    }
    header.total_packet_size = sizeof_packet_header();
    header.total_packet_size += strlen(packet.client_name) + 1;

    serializer_t serializer = {};
    initialize_serializer(&serializer, header.total_packet_size);
    serialize_packet_header(&serializer, &header);
    serialize_client_join_packet(&serializer, &packet);
    
    network_address_t server_address { (uint16_t)host_to_network_byte_order(g_network_state->GAME_OUTPUT_PORT_SERVER), str_to_ipv4_int32(ip_address) };
    send_serialized_message(&serializer, server_address);

    g_network_state->server_address = server_address;
}

internal_function int32_t lua_join_server(lua_State *state);
internal_function int32_t lua_toggle_freeze_receiver_thread(lua_State *state);
internal_function int32_t lua_toggle_freeze_client_after_input(lua_State *state);

void initialize_as_client(void)
{
    add_network_socket(&g_network_state->main_network_socket);
    initialize_network_socket(&g_network_state->main_network_socket, AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    network_address_t address = {};
    address.port = host_to_network_byte_order(g_network_state->GAME_OUTPUT_PORT_CLIENT);
    bind_network_socket_to_port(&g_network_state->main_network_socket, address);
    set_socket_to_non_blocking_mode(&g_network_state->main_network_socket);
    add_global_to_lua(script_primitive_type_t::FUNCTION, "join_server", &lua_join_server);
    add_global_to_lua(script_primitive_type_t::FUNCTION, "toggle_freeze_receiver_thread", &lua_toggle_freeze_receiver_thread);
    add_global_to_lua(script_primitive_type_t::FUNCTION, "freeze_after_input_packet_sent", &lua_toggle_freeze_client_after_input);

    g_network_state->player_state_cbuffer.initialize(40);
    g_network_state->player_state_history.initialize(60);
}

internal_function void receiver_thread_process(void *receiver_thread_data)
{
    output_to_debug_console("Started receiver thread\n");
    
    receiver_thread_t *process_data = (receiver_thread_t *)receiver_thread_data;

    for (;;)
    {
        if (wait_for_mutex_and_own(process_data->mutex) && !process_data->receiver_freezed)
        {
            if (process_data->receiver_thread_loop_count == 0)
            {
            }
            
            ++(process_data->receiver_thread_loop_count);
            // ^^^^^^^^^^ debug stuff ^^^^^^^^^^^^

            char *message_buffer = (char *)process_data->packet_allocator.current;
        
            network_address_t received_address = {};
            int32_t bytes_received = receive_from(&g_network_state->main_network_socket, message_buffer, process_data->packet_allocator.capacity - process_data->packet_allocator.used_capacity, &received_address);

            if (bytes_received > 0)
            {
                // Actually officially allocate memory on linear allocator (even though it will already have been filled)
                process_data->packets[process_data->packet_count] = allocate_linear(bytes_received, 1, "", &process_data->packet_allocator);
                process_data->packet_sizes[process_data->packet_count] = bytes_received;
                process_data->addresses[process_data->packet_count] = received_address;

                ++(process_data->packet_count);
            }

            release_mutex(process_data->mutex);
        }
    }
}

internal_function void initialize_receiver_thread(void)
{
    g_network_state->receiver_thread.packet_allocator.capacity = megabytes(30);
    g_network_state->receiver_thread.packet_allocator.start = g_network_state->receiver_thread.packet_allocator.current = malloc(g_network_state->receiver_thread.packet_allocator.capacity);

    g_network_state->receiver_thread.mutex = request_mutex();
    request_thread_for_process(&receiver_thread_process, &g_network_state->receiver_thread);
}

void initialize_as_server(void)
{
     add_network_socket(&g_network_state->main_network_socket);
     initialize_network_socket(&g_network_state->main_network_socket, AF_INET, SOCK_DGRAM, IPPROTO_UDP);
     network_address_t address = {};
     address.port = host_to_network_byte_order(g_network_state->GAME_OUTPUT_PORT_SERVER);
     bind_network_socket_to_port(&g_network_state->main_network_socket, address);
     set_socket_to_non_blocking_mode(&g_network_state->main_network_socket);

     //initialize_receiver_thread();
}

void initialize_network_translation_unit(struct game_memory_t *memory)
{
    g_network_state = &memory->network_state;
}

// Adds a client to the network_component_t array in entities_t
uint32_t add_client(network_address_t network_address, const char *client_name, player_handle_t player_handle)
{
    // Initialize network component or something

    return(0);
}

client_t *get_client(uint32_t index)
{
    return(&g_network_state->clients[index]);
}

#define MAX_MESSAGE_BUFFER_SIZE 40000
global_var char message_buffer[MAX_MESSAGE_BUFFER_SIZE] = {};


void send_chunks_hard_update_packets(network_address_t address)
{
    serializer_t chunks_serializer = {};
    initialize_serializer(&chunks_serializer, (sizeof(uint8_t) * 3 + sizeof(uint8_t) * VOXEL_CHUNK_EDGE_LENGTH * VOXEL_CHUNK_EDGE_LENGTH * VOXEL_CHUNK_EDGE_LENGTH) * 8 /* Maximum amount of chunks to "hard update per packet" */);

    packet_header_t header = {};
    header.packet_mode = packet_mode_t::PM_SERVER_MODE;
    header.packet_type = server_packet_type_t::SPT_CHUNK_VOXELS_HARD_UPDATE;
    // TODO: Increment this with every packet sent
    header.current_tick = *get_current_tick();

    uint32_t hard_update_count = 0;
    voxel_chunk_values_packet_t *voxel_update_packets = initialize_chunk_values_packets(&hard_update_count);

    uint32_t loop_count = (hard_update_count / 8);
    for (uint32_t packet = 0; packet < loop_count; ++packet)
    {
        voxel_chunk_values_packet_t *pointer = &voxel_update_packets[packet * 8];

        header.total_packet_size = sizeof_packet_header() + sizeof(uint32_t) + sizeof(uint8_t) * VOXEL_CHUNK_EDGE_LENGTH * VOXEL_CHUNK_EDGE_LENGTH * VOXEL_CHUNK_EDGE_LENGTH * 8;
        serialize_packet_header(&chunks_serializer, &header);

        serialize_uint32(&chunks_serializer, 8);
        
        for (uint32_t chunk = 0; chunk < 8; ++chunk)
        {
            serialize_voxel_chunk_values_packet(&chunks_serializer, &pointer[chunk]);
        }

        send_serialized_message(&chunks_serializer, address);
        chunks_serializer.data_buffer_head = 0;

        hard_update_count -= 8;
    }

    if (hard_update_count)
    {
        voxel_chunk_values_packet_t *pointer = &voxel_update_packets[loop_count * 8];

        header.total_packet_size = sizeof_packet_header() + sizeof(uint8_t) * VOXEL_CHUNK_EDGE_LENGTH * VOXEL_CHUNK_EDGE_LENGTH * VOXEL_CHUNK_EDGE_LENGTH * hard_update_count;
        serialize_packet_header(&chunks_serializer, &header);

        serialize_uint32(&chunks_serializer, hard_update_count);
        
        for (uint32_t chunk = 0; chunk < hard_update_count; ++chunk)
        {
            serialize_voxel_chunk_values_packet(&chunks_serializer, &pointer[chunk]);
        }

        
        send_serialized_message(&chunks_serializer, address);
    }
}

void dispatch_newcoming_client_to_clients(uint32_t new_client_index)
{
    client_t *newcoming_client = get_client(new_client_index);
    player_t *player = get_player(newcoming_client->player_handle);
    
    serializer_t serializer = {};
    initialize_serializer(&serializer, 80);

    packet_header_t header = {};
    header.packet_mode = packet_mode_t::PM_SERVER_MODE;
    header.packet_type = server_packet_type_t::SPT_CLIENT_JOINED;
    header.current_tick = *get_current_tick();

    serialize_packet_header(&serializer, &header);

    player_state_initialize_packet_t player_initialize_packet = {};
    player_initialize_packet.client_id = new_client_index;
    player_initialize_packet.player_name = newcoming_client->name;
    player_initialize_packet.ws_position = player->ws_p;
    player_initialize_packet.ws_direction = player->ws_d;
    
    serialize_player_state_initialize_packet(&serializer, &player_initialize_packet);

    for (uint32_t client_index = 0; client_index < g_network_state->client_count; ++client_index)
    {
        client_t *current_client = get_client(client_index);

        send_serialized_message(&serializer, current_client->network_address);
    }
}

void dispatch_snapshot_to_clients(void)
{
    packet_header_t header = {};
    header.packet_mode = packet_mode_t::PM_SERVER_MODE;
    header.packet_type = server_packet_type_t::SPT_GAME_STATE_SNAPSHOT;
 
    game_snapshot_player_state_packet_t player_snapshots[MAX_CLIENTS] = {};
    
    serializer_t out_serializer = {};
    initialize_serializer(&out_serializer, 2000);

    // Prepare the player snapshot packets
    for (uint32_t client_index = 0; client_index < g_network_state->client_count; ++client_index)
    {
        client_t *client = get_client(client_index);
        player_t *player = get_player(client->player_handle);

        player_snapshots[client_index].client_id = client->client_id;
        player_snapshots[client_index].ws_position = player->ws_p;
        player_snapshots[client_index].ws_direction = player->ws_d;
        player_snapshots[client_index].ws_velocity = player->ws_v;
        player_snapshots[client_index].ws_rotation = player->ws_r;
    }
    
    for (uint32_t client_index = 0; client_index < g_network_state->client_count; ++client_index)
    {
        client_t *client = get_client(client_index);
        player_t *player = get_player(client->player_handle);

        if (client->received_input_commands)
        {
            game_snapshot_player_state_packet_t *player_snapshot_packet = &player_snapshots[client_index];

            out_serializer.data_buffer_head = 0;

            header.total_packet_size = sizeof_packet_header() +
                sizeof(uint64_t) +
                sizeof_game_snapshot_player_state_packet() * g_network_state->client_count;
        
            serialize_packet_header(&out_serializer, &header);

            player_state_t *previous_received_player_state = &client->previous_received_player_state;

            uint64_t previous_received_tick = client->previous_client_tick;
            serialize_uint64(&out_serializer, previous_received_tick);
            
            for (uint32_t i = 0; i < g_network_state->client_count; ++i)
            {
                if (i == client_index)
                {
                    if (client->received_input_commands)
                    {
                        // Do comparison to determine if client correction is needed
                        float32_t precision = 0.1f;
                        vector3_t ws_position_difference = glm::abs(previous_received_player_state->ws_position - player_snapshot_packet->ws_position);
                        vector3_t ws_direction_difference = glm::abs(previous_received_player_state->ws_direction - player_snapshot_packet->ws_direction);
                        if (ws_position_difference.x > precision ||
                            ws_position_difference.y > precision ||
                            ws_position_difference.z > precision ||
                            ws_direction_difference.x > precision ||
                            ws_direction_difference.y > precision ||
                            ws_direction_difference.z > precision)
                        {
                            // Make sure that server invalidates all packets previously sent by the client
                            player->network.player_states_cbuffer.tail = player->network.player_states_cbuffer.head;
                            player->network.player_states_cbuffer.head_tail_difference = 0;
                            
                            // Force client to do correction
                            player_snapshot_packet->need_to_do_correction = 1;

                            // Server will now wait until reception of a prediction error packet
                            client->needs_to_acknowledge_prediction_error = 1;

                            //output_to_debug_console("Client ", (int32_t)client_index, "(", client->name, ")", " needs to correct | ", previous_received_player_state->ws_position, " -> ", player_snapshot_packet->ws_position, "\n");
                        }
                        else
                        {
                            player_snapshot_packet->need_to_do_correction = 0;
                        }

                        player_snapshot_packet->is_to_ignore = 0;
                    }
                    else
                    {
                        player_snapshot_packet->is_to_ignore = 1;
                    }
                }

                serialize_game_snapshot_player_state_packet(&out_serializer, &player_snapshots[i]);
            }
        
            send_serialized_message(&out_serializer, client->network_address);
        }
    }

    for (uint32_t client_index = 0; client_index < g_network_state->client_count; ++client_index)
    {
        output_to_debug_console(g_network_state->clients[client_index].name, ": ", player_snapshots[client_index].ws_rotation, " ; ");
    }

    output_to_debug_console("\n");
}

float32_t get_snapshot_server_rate(void)
{
    return(g_network_state->server_game_state_snapshot_rate);
}

// Might have to be done on a separate thread just for updating world data
void update_as_server(input_state_t *input_state, float32_t dt)
{    
    //if (wait_for_mutex_and_own(g_network_state->receiver_thread.mutex))
    {
        // Send Snapshots (25 per second)
        // Every 40ms (25 sps) - basically every frame
        // TODO: Add function to send snapshot
        persist_var float32_t time_since_previous_snapshot = 0.0f;
    
        // Send stuff out to the clients (game state and stuff...)
        // TODO: Add changeable
    
        time_since_previous_snapshot += dt;
        float32_t max_time = 1.0f / g_network_state->server_game_state_snapshot_rate; // 20 per second
        
        if (time_since_previous_snapshot > max_time)
        {
            // Dispath game state to all clients
            dispatch_snapshot_to_clients();
            
            time_since_previous_snapshot = 0.0f;
        }
        
        g_network_state->receiver_thread.receiver_thread_loop_count = 0;

        //        for (uint32_t packet_index = 0; packet_index < g_network_state->receiver_thread.packet_count; ++packet_index)
        for (uint32_t i = 0; i < 1 + 2 * g_network_state->client_count; ++i)
        {
            //network_address_t received_address = g_network_state->receiver_thread.addresses[packet_index];
            
            //serializer_t in_serializer = {};
            //in_serializer.data_buffer = (uint8_t *)(g_network_state->receiver_thread.packets[packet_index]);
            //in_serializer.data_buffer_size = g_network_state->receiver_thread.packet_sizes[packet_index];

            network_address_t received_address = {};
            int32_t bytes_received = receive_from(&g_network_state->main_network_socket, message_buffer, MAX_MESSAGE_BUFFER_SIZE, &received_address);

            if (bytes_received > 0)
            {
                serializer_t in_serializer = {};
                in_serializer.data_buffer = (uint8_t *)message_buffer;
                in_serializer.data_buffer_size = bytes_received;
                
                packet_header_t header = {};
                deserialize_packet_header(&in_serializer, &header);

                if (header.total_packet_size == in_serializer.data_buffer_size)
                {
                    if (header.packet_mode == packet_mode_t::PM_CLIENT_MODE)
                    {
                        switch (header.packet_type)
                        {
                        case client_packet_type_t::CPT_CLIENT_JOIN:
                            {

                                client_join_packet_t client_join = {};
                                deserialize_client_join_packet(&in_serializer, &client_join);

                                // Add client
                                client_t *client = get_client(g_network_state->client_count);
                                client->name = client_join.client_name;
                                client->client_id = g_network_state->client_count;
                                client->network_address = received_address;
                                client->current_packet_count = 0;

                                // Add the player to the actual entities list (spawn the player in the world)
                                client->player_handle = spawn_player(client->name, player_color_t::GRAY, client->client_id);
                                player_t *local_player_data_ptr = get_player(client->player_handle);
                                local_player_data_ptr->network.client_state_index = client->client_id;

                                ++g_network_state->client_count;
                                constant_string_t constant_str_name = make_constant_string(client_join.client_name, strlen(client_join.client_name));
                                g_network_state->client_table_by_name.insert(constant_str_name.hash, client->client_id);
                                g_network_state->client_table_by_address.insert(received_address.ipv4_address, client->client_id);
                        
                                // LOG:
                                console_out(client_join.client_name);
                                console_out(" joined the game!\n");
                        
                                // Reply - Create handshake packet
                                serializer_t out_serializer = {};
                                initialize_serializer(&out_serializer, 2000);
                                game_state_initialize_packet_t game_state_init_packet = {};
                                initialize_game_state_initialize_packet(&game_state_init_packet, client->client_id);


                        
                                packet_header_t handshake_header = {};
                                handshake_header.packet_mode = packet_mode_t::PM_SERVER_MODE;
                                handshake_header.packet_type = server_packet_type_t::SPT_SERVER_HANDSHAKE;
                                handshake_header.total_packet_size = sizeof_packet_header();
                                handshake_header.total_packet_size += sizeof(voxel_state_initialize_packet_t);
                                handshake_header.total_packet_size += sizeof(game_state_initialize_packet_t::client_index) + sizeof(game_state_initialize_packet_t::player_count);
                                handshake_header.total_packet_size += sizeof(player_state_initialize_packet_t) * game_state_init_packet.player_count;
                                handshake_header.current_tick = *get_current_tick();
                        
                                serialize_packet_header(&out_serializer, &handshake_header);
                                serialize_game_state_initialize_packet(&out_serializer, &game_state_init_packet);
                                send_serialized_message(&out_serializer, client->network_address);

                                send_chunks_hard_update_packets(client->network_address);

                                dispatch_newcoming_client_to_clients(client->client_id);
                        
                            } break;

                        case client_packet_type_t::CPT_INPUT_STATE:
                            {
                                client_t *client = get_client(header.client_id);
                                if (!client->needs_to_acknowledge_prediction_error)
                                {
                                    client->received_input_commands = 1;
                        
                                    // Current client tick (will be used for the snapshot that will be sent to the clients)
                                    // Clients will compare the state at the tick that the server recorded as being the last client tick at which server received input state (commands)
                                    client->previous_client_tick = header.current_tick;

                                    player_t *player = get_player(client->player_handle);
                        
                                    uint32_t player_state_count = deserialize_uint32(&in_serializer);

                                    player_state_t last_player_state = {};
                        
                                    for (uint32_t i = 0; i < player_state_count; ++i)
                                    {
                                        client_input_state_packet_t input_packet = {};
                                        player_state_t player_state = {};
                                        deserialize_client_input_state_packet(&in_serializer, &input_packet);

                                        player_state.action_flags = input_packet.action_flags;
                                        player_state.mouse_x_diff = input_packet.mouse_x_diff;
                                        player_state.mouse_y_diff = input_packet.mouse_y_diff;
                                        player_state.flags_byte = input_packet.flags_byte;
                                        player_state.dt = input_packet.dt;

                                        if (player_state.action_flags)
                                        {
                                            //                                            __debugbreak();
                                        }

                                        player->network.player_states_cbuffer.push_item(&player_state);

                                        last_player_state = player_state;
                                    }

                                    // Will use the data in here to check whether the client needs correction or not
                                    client->previous_received_player_state = last_player_state;

                                    client->previous_received_player_state.ws_position = deserialize_vector3(&in_serializer);
                                    client->previous_received_player_state.ws_direction = deserialize_vector3(&in_serializer);

                                    player->network.commands_to_flush += player_state_count;
                                }
                            } break;
                        case client_packet_type_t::CPT_PREDICTION_ERROR_CORRECTION:
                            {
                                client_t *client = get_client(header.client_id);
                                client->needs_to_acknowledge_prediction_error = 0;

                                player_t *player = get_player(client->player_handle);

                                client->previous_client_tick = deserialize_uint64(&in_serializer);
                                client->received_input_commands = 0;

                                output_to_debug_console("Client ", (int32_t)client->client_id, "(", client->name, ")", " did correction | is now at ", player->ws_p, "\n");
                            } break;
                        case client_packet_type_t::CPT_ACKNOWLEDGED_GAME_STATE_RECEPTION:
                            {
                                uint64_t game_state_acknowledged_tick = deserialize_uint64(&in_serializer);
                                client_t *client = get_client(header.client_id);
                        
                            } break;
                        }
                    }
                }
            }
        }

        //g_network_state->receiver_thread.packet_count = 0;
        //clear_linear(&g_network_state->receiver_thread.packet_allocator);
        //release_mutex(g_network_state->receiver_thread.mutex);
    }
}

 
internal_function void send_client_action_flags(void)
{
    //    if (!g_network_state->client_will_freeze_after_input && !g_network_state->sent_active_action_flags)
    {
        player_t *user = get_user_player();    

        packet_header_t header = {};
    
        header.packet_mode = packet_mode_t::PM_CLIENT_MODE;
        header.packet_type = client_packet_type_t::CPT_INPUT_STATE;
    
        header.total_packet_size = sizeof_packet_header() + sizeof(uint32_t) + sizeof_client_input_state_packet() * g_network_state->player_state_cbuffer.head_tail_difference + sizeof(vector3_t) * 2 /* These two vectors are the outputs of the commands */;
        header.client_id = user->network.client_state_index;
        header.current_tick = *get_current_tick();

        serializer_t serializer = {};
        initialize_serializer(&serializer, header.total_packet_size);
    
        serialize_packet_header(&serializer, &header);

        serialize_uint32(&serializer, g_network_state->player_state_cbuffer.head_tail_difference);

        uint32_t player_states_to_send = g_network_state->player_state_cbuffer.head_tail_difference;

        player_state_t *state;
        for (uint32_t i = 0; i < player_states_to_send; ++i)
        {
            state = g_network_state->player_state_cbuffer.get_next_item();
            serialize_client_input_state_packet(&serializer, state);
        }

        player_state_t to_store = *state;
        to_store.ws_position = user->ws_p;
        to_store.ws_direction = user->ws_d;
        to_store.tick = header.current_tick;
        //g_network_state->player_state_history.push_item(&to_store);

        serialize_vector3(&serializer, to_store.ws_position);
        serialize_vector3(&serializer, to_store.ws_direction);

        send_serialized_message(&serializer, g_network_state->server_address);

        if (to_store.action_flags)
        {
            g_network_state->sent_active_action_flags = 1;
        }
    }
}


void buffer_player_state(float32_t dt)
{
    player_t *user = get_user_player();
    player_state_t player_state = initialize_player_state(user);
    player_state.dt = dt;

    g_network_state->player_state_cbuffer.push_item(&player_state);
}


void fill_last_player_state_if_needed(player_t *player)
{
    if (g_network_state->fill_requested)
    {
        uint32_t head = g_network_state->player_state_history.head;
        if (!head)
        {
            head = g_network_state->player_state_history.buffer_size - 1;
        }
        else
        {
            --head;
        }
    
        player_state_t *player_state = &g_network_state->player_state_history.buffer[head];

        player_state->ws_position = player->ws_p;
        player_state->ws_direction = player->ws_d;

        g_network_state->fill_requested = 0;
    }
}


internal_function void send_prediction_error_correction(uint64_t tick)
{
    serializer_t serializer = {};
    initialize_serializer(&serializer, sizeof_packet_header() + sizeof(uint64_t));

    player_t *user = get_user_player();
    
    packet_header_t header = {};
    header.packet_mode = PM_CLIENT_MODE;
    header.packet_type = CPT_PREDICTION_ERROR_CORRECTION;
    header.total_packet_size = sizeof_packet_header() + sizeof(uint64_t);
    header.current_tick = tick;
    header.client_id = user->network.client_state_index;
    
    serialize_packet_header(&serializer, &header);
    serialize_uint64(&serializer, tick);

    send_serialized_message(&serializer, g_network_state->server_address);

    *get_current_tick() = tick;
}


void update_as_client(input_state_t *input_state, float32_t dt)
{
    persist_var float32_t time_since_last_input_state = 0.0f;
    
    // Send stuff out to the server (input state and stuff...)
    // TODO: Add changeable
    if (g_network_state->is_connected_to_server)
    {
        time_since_last_input_state += dt;
        float32_t max_time = 1.0f / g_network_state->client_input_snapshot_rate;
        
        if (time_since_last_input_state > max_time)
        {
            send_client_action_flags();

            time_since_last_input_state = 0.0f;
        }
    }

    network_address_t received_address = {};
    bool received = receive_from(&g_network_state->main_network_socket, message_buffer, sizeof(message_buffer), &received_address);

    if (received)
    {
        serializer_t in_serializer = {};
        in_serializer.data_buffer = (uint8_t *)message_buffer;
        in_serializer.data_buffer_size = MAX_MESSAGE_BUFFER_SIZE;

        packet_header_t header = {};
        deserialize_packet_header(&in_serializer, &header);

        if (header.packet_mode == packet_mode_t::PM_SERVER_MODE)
        {
            switch(header.packet_type)
            {
            case server_packet_type_t::SPT_SERVER_HANDSHAKE:
                {

                    *get_current_tick() = header.current_tick;
                    
                    game_state_initialize_packet_t game_state_init_packet = {};
                    deserialize_game_state_initialize_packet(&in_serializer, &game_state_init_packet);
                    
                    deinitialize_world();
                    
                    initialize_world(&game_state_init_packet, input_state);

                    // Add client structs (indices of the structs on the server will be the same as on the client)
                    g_network_state->client_count = game_state_init_packet.player_count;

                    // Existing players when client joined the game
                    for (uint32_t i = 0; i < game_state_init_packet.player_count; ++i)
                    { 
                       player_state_initialize_packet_t *player_packet = &game_state_init_packet.player[i];
                        player_t *player = get_player(player_packet->player_name);

                        client_t *client = get_client(player_packet->client_id);
                        client->name = player->id.str;
                        client->client_id = player_packet->client_id;
                        client->player_handle = player->index;

                        if (client->client_id != game_state_init_packet.client_index)
                        {
                            player->network.is_remote = 1;
                            player->network.max_time = 1.0f / get_snapshot_server_rate();
                        }
                    }

                    g_network_state->is_connected_to_server = 1;

                } break;
            case server_packet_type_t::SPT_CHUNK_VOXELS_HARD_UPDATE:
                {
                    
                    uint32_t chunks_to_update = deserialize_uint32(&in_serializer);
                    for (uint32_t i = 0; i < chunks_to_update; ++i)
                    {
                        voxel_chunk_values_packet_t packet = {};
                        deserialize_voxel_chunk_values_packet(&in_serializer, &packet);
                        
                        voxel_chunk_t *chunk = *get_voxel_chunk(packet.chunk_coord_x, packet.chunk_coord_y, packet.chunk_coord_z);
                        memcpy(chunk->voxels, packet.voxels, sizeof(uint8_t) * VOXEL_CHUNK_EDGE_LENGTH * VOXEL_CHUNK_EDGE_LENGTH * VOXEL_CHUNK_EDGE_LENGTH);

                        ready_chunk_for_gpu_sync(chunk);
                    }
                    
                } break;
            case server_packet_type_t::SPT_GAME_STATE_SNAPSHOT:
                {
                    uint64_t previous_tick = deserialize_uint64(&in_serializer);

                    for (uint32_t i = 0; i < g_network_state->client_count; ++i)
                    {
                        // This is the player state to compare with what the server sent
                        game_snapshot_player_state_packet_t player_snapshot_packet = {};
                        deserialize_game_snapshot_player_state_packet(&in_serializer, &player_snapshot_packet);

                        client_t *client = get_client(player_snapshot_packet.client_id);
                        player_t *current_player = get_player(client->player_handle);

                        player_t *local_user = get_user_player();

                        // We are dealing with the local client: we need to deal with the correction stuff
                        if (local_user->network.client_state_index == player_snapshot_packet.client_id && !player_snapshot_packet.is_to_ignore)
                        {
                            if (player_snapshot_packet.need_to_do_correction)
                            {                                        
                                // Do correction here
                                player_t *player = get_user_player();
                                player->ws_p = player_snapshot_packet.ws_position;
                                player->ws_d = player_snapshot_packet.ws_direction;
                                player->ws_v = player_snapshot_packet.ws_velocity;

                                output_to_debug_console("Client ", (int32_t)local_user->network.client_state_index, "(", client->name, ")", " did correction | now at ", local_user->ws_p, "\n\n");

                                // Send a prediction error correction packet
                                send_prediction_error_correction(previous_tick);
                            }
                        }
                        // We are dealing with a remote client: we need to deal with entity interpolation stuff
                        else
                        {
                            remote_player_snapshot_t remote_player_snapshot = {};
                            remote_player_snapshot.ws_position = player_snapshot_packet.ws_position;
                            remote_player_snapshot.ws_direction = player_snapshot_packet.ws_direction;
                            remote_player_snapshot.ws_rotation = player_snapshot_packet.ws_rotation;
                            current_player->network.remote_player_states.push_item(&remote_player_snapshot);
                        }

                        output_to_debug_console(current_player->id.str, ": ", current_player->ws_r, " ; ");
                    }

                    output_to_debug_console("\n");
                    
                } break;
            case server_packet_type_t::SPT_CLIENT_JOINED:
                {
                    player_state_initialize_packet_t new_client_init_packet = {};
                    deserialize_player_state_initialize_packet(&in_serializer, &new_client_init_packet);

                    player_t *user = get_user_player();

                    
                    // In case server sends init data to the user
                    if (new_client_init_packet.client_id != user->network.client_state_index)
                    {
                        ++g_network_state->client_count;
                        
                        player_handle_t new_player_handle = initialize_player_from_player_init_packet(user->network.client_state_index, &new_client_init_packet);

                        player_t *player = get_player(new_player_handle);

                        // Sync the network.cpp's client data with the world.cpp's player data
                        client_t *client = get_client(new_client_init_packet.client_id);
                        client->name = player->id.str;
                        client->client_id = new_client_init_packet.client_id;
                        client->player_handle = player->index;

                        player->network.is_remote = 1;
                        player->network.max_time = 1.0f / get_snapshot_server_rate();

                        console_out(client->name, " joined the game!\n");
                    }
                } break;
            }
        }
    }
}

void update_network_state(input_state_t *input_state, float32_t dt)
{
    switch(g_network_state->current_app_mode)
    {
    case application_mode_t::CLIENT_MODE: { update_as_client(input_state, dt); } break;
    case application_mode_t::SERVER_MODE: { update_as_server(input_state, dt); } break;
    }
}

void initialize_network_state(game_memory_t *memory, application_mode_t app_mode)
{
    initialize_socket_api();
    g_network_state->current_app_mode = app_mode;

    switch(g_network_state->current_app_mode)
    {
    case application_mode_t::CLIENT_MODE: { initialize_as_client(); } break;
    case application_mode_t::SERVER_MODE: { initialize_as_server(); } break;
    }
}

internal_function int32_t lua_join_server(lua_State *state)
{
    const char *ip_address = lua_tostring(state, -2);
    const char *user_name = lua_tostring(state, -1);

    join_server(ip_address, user_name);

    return(0);
}

internal_function int32_t lua_toggle_freeze_receiver_thread(lua_State *state)
{
    g_network_state->receiver_thread.receiver_freezed ^= 1;

    return(0);
}

internal_function int32_t lua_toggle_freeze_client_after_input(lua_State *state)
{
    g_network_state->client_will_freeze_after_input ^= 1;

    return(0);
}
