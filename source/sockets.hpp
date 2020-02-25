#pragma once

#include "utility.hpp"


void initialize_socket_api(uint16_t output_port);


// Easier to use this in Vulkan-style handle instead of a class because of different platform implementations of sockets API
struct network_socket_t {
    int32_t socket;
};


struct network_address_t {
    uint16_t port;
    uint32_t ipv4_address;
};


void add_network_socket(network_socket_t *socket);
void initialize_network_socket(network_socket_t *socket, int32_t family, int32_t type, int32_t protocol);
void bind_network_socket_to_port(network_socket_t *socket, network_address_t address);
void set_socket_to_non_blocking_mode(network_socket_t *socket);


int32_t receive_from(char *buffer, uint32_t buffer_size, network_address_t *address_dst);
bool send_to(network_address_t address, char *buffer, uint32_t buffer_size);


uint32_t str_to_ipv4_int32(const char *address);
uint32_t host_to_network_byte_order(uint32_t bytes);
uint32_t network_to_host_byte_order(uint32_t bytes);
float32_t host_to_network_byte_order_f32(float32_t bytes);
float32_t network_to_host_byte_order_f32(float32_t bytes);
