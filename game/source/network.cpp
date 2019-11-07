#include <cassert>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <Windows.h>
#include "network.hpp"
#include "game.hpp"
#include "ui.hpp"
#include "script.hpp"
#include "core.hpp"

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
        assert(0);
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
        //OutputDebugString("recvfrom failed\n");
        return -1;
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
    uint8_t *pointer = grow_serializer_data_buffer(serializer, 4);
    *pointer = u8;
}

uint8_t deserialize_uint8(serializer_t *serializer)
{
    uint8_t *pointer = grow_serializer_data_buffer(serializer, 4);
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
}


void deserialize_packet_header(serializer_t *serializer, packet_header_t *packet)
{
    packet->bytes = deserialize_uint32(serializer);
}


void serialize_client_join_packet(serializer_t *serializer, client_join_packet_t *packet)
{
    serialize_string(serializer, packet->client_name);
}


void deserialize_client_join_packet(serializer_t *serializer, client_join_packet_t *packet)
{
    packet->client_name = deserialize_string(serializer);
}

void serialize_player_state_initialize_packet(serializer_t *serializer, player_state_initialize_packet_t *packet)
{
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
    header.total_packet_size = sizeof(packet_header_t::bytes);
    header.total_packet_size += strlen(packet.client_name) + 1;

    serializer_t serializer = {};
    initialize_serializer(&serializer, header.total_packet_size);
    serialize_packet_header(&serializer, &header);
    serialize_client_join_packet(&serializer, &packet);
    
    network_address_t server_address { (uint16_t)host_to_network_byte_order(g_network_state->GAME_OUTPUT_PORT_SERVER), str_to_ipv4_int32(ip_address) };
    send_serialized_message(&serializer, server_address);
}

internal_function int32_t lua_join_server(lua_State *state);

void initialize_as_client(void)
{
    add_network_socket(&g_network_state->main_network_socket);
    initialize_network_socket(&g_network_state->main_network_socket, AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    network_address_t address = {};
    address.port = host_to_network_byte_order(g_network_state->GAME_OUTPUT_PORT_CLIENT);
    bind_network_socket_to_port(&g_network_state->main_network_socket, address);
    set_socket_to_non_blocking_mode(&g_network_state->main_network_socket);
    add_global_to_lua(script_primitive_type_t::FUNCTION, "join_server", &lua_join_server);

    join_server("127.0.0.1", "Walter Sobschak");
}

void initialize_as_server(void)
{
     add_network_socket(&g_network_state->main_network_socket);
     initialize_network_socket(&g_network_state->main_network_socket, AF_INET, SOCK_DGRAM, IPPROTO_UDP);
     network_address_t address = {};
     address.port = host_to_network_byte_order(g_network_state->GAME_OUTPUT_PORT_SERVER);
     bind_network_socket_to_port(&g_network_state->main_network_socket, address);
     set_socket_to_non_blocking_mode(&g_network_state->main_network_socket);
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

#define MAX_MESSAGE_BUFFER_SIZE 3000
global_var char message_buffer[MAX_MESSAGE_BUFFER_SIZE] = {};

// Might have to be done on a separate thread just for updating world data
void update_as_server(void)
{
    network_address_t received_address = {};
    int32_t bytes_received = receive_from(&g_network_state->main_network_socket, message_buffer, sizeof(message_buffer), &received_address);

    if (bytes_received > 0)
    {
        serializer_t in_serializer = {};
        in_serializer.data_buffer = (uint8_t *)message_buffer;
        in_serializer.data_buffer_size = MAX_MESSAGE_BUFFER_SIZE;

        packet_header_t header = {};
        deserialize_packet_header(&in_serializer, &header);

        if (header.total_packet_size == bytes_received)
        {
            if (header.packet_mode == packet_mode_t::PM_CLIENT_MODE)
            {
                switch (header.packet_type)
                {
                case client_packet_type_t::CPT_CLIENT_JOIN:
                    {

                        client_join_packet_t client_join = {};
                        deserialize_client_join_packet(&in_serializer, &client_join);

                        // Create handshake packet
                        serializer_t out_serializer = {};
                        initialize_serializer(&out_serializer, 1000);
                        game_state_initialize_packet_t game_state_init_packet = {};
                        initialize_game_state_initialize_packet(&game_state_init_packet, 0);

                        packet_header_t handshake_header = {};
                        handshake_header.packet_mode = packet_mode_t::PM_SERVER_MODE;
                        handshake_header.packet_type = server_packet_type_t::SPT_SERVER_HANDSHAKE;
                        handshake_header.total_packet_size = sizeof(packet_header_t::bytes);
                        handshake_header.total_packet_size += sizeof(voxel_state_initialize_packet_t);
                        handshake_header.total_packet_size += sizeof(game_state_initialize_packet_t::client_index) + sizeof(game_state_initialize_packet_t::player_count);
                        handshake_header.total_packet_size += sizeof(player_state_initialize_packet_t) * game_state_init_packet.player_count;
                        
                        serialize_packet_header(&out_serializer, &handshake_header);
                        serialize_game_state_initialize_packet(&out_serializer, &game_state_init_packet);
                        send_serialized_message(&out_serializer, received_address);

                    } break;
                    // case packet_header_t::client_packet_type_t::ETC:
                }
            }
        }
        else
        {
            // Handle UDP packet split (*sad face*)
        }
    }
}

void update_as_client(void)
{
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

                    game_state_initialize_packet_t game_state_init_packet = {};
                    deserialize_game_state_initialize_packet(&in_serializer, &game_state_init_packet);
                    
                    deinitialize_world();

                } break;
            }
        }
    }
}

void update_network_state(void)
{
    switch(g_network_state->current_app_mode)
    {
    case application_mode_t::CLIENT_MODE: { update_as_client(); } break;
    case application_mode_t::SERVER_MODE: { update_as_server(); } break;
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
    const char *string = lua_tostring(state, -1);

    join_server(string, "Walter Sobschak");

    return(0);
}
