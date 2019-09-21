#pragma once

#include "core.hpp"

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

bool receive_from(network_socket_t *socket, char *buffer, uint32_t buffer_size, network_address_t *address_dst);
bool send_to(network_socket_t *socket, network_address_t address, char *buffer, uint32_t buffer_size);

uint32_t str_to_ipv4_int32(const char *address);
uint32_t host_to_network_byte_order(uint32_t bytes);
uint32_t network_to_host_byte_order(uint32_t bytes);

void initialize_socket_api(void);

struct socket_manager_t
{
    static constexpr uint32_t MAX_SOCKETS = 50;
    SOCKET sockets[MAX_SOCKETS] = {};
    uint32_t socket_count = 0;
};

struct packet_header_t
{
    enum packet_mode_t { CLIENT_MODE, SERVER_MODE };
    enum client_packet_type_t { CLIENT_JOIN };
    enum server_packet_type_t { SERVER_HANDSHAKE };

    uint32_t packet_mode: 1;
    uint32_t packet_type: 4 /* To increase in the future when more packet types appear */;
    // Includes header
    uint32_t total_packet_size: 27;
};

// This also needs to send the state of the world
struct server_handshake_packet_t
{
    packet_header_t header;
    uint16_t client_id;
};

#define CLIENT_NAME_MAX_LENGTH 40

struct client_join_packet_t
{
    packet_header_t header;
    char client_name[CLIENT_NAME_MAX_LENGTH];
};

struct client_state_t
{
    // Name, id, etc...
    char client_name[CLIENT_NAME_MAX_LENGTH];
    uint16_t client_id;
    network_address_t network_address;
};

#define MAX_CLIENTS 40

struct network_state_t
{
    enum application_mode_t { CLIENT_MODE, SERVER_MODE };
    application_mode_t current_app_mode;

    const uint16_t GAME_OUTPUT_PORT_SERVER = 6000;
    const uint16_t GAME_OUTPUT_PORT_CLIENT = 6001;
    network_socket_t main_network_socket;

    // API stuff
    socket_manager_t sockets;

    uint32_t client_count;
    client_state_t clients[MAX_CLIENTS];
};

void update_network_state(void);

void initialize_network_translation_unit(struct game_memory_t *memory);
void initialize_network_state(struct game_memory_t *memory, network_state_t::application_mode_t app_mode);
