#pragma once

#include "utils.hpp"
#include "sockets.hpp"
#include "packets.hpp"

struct serializer_t {
    uint32_t data_buffer_size;
    uint8_t *data_buffer;
    uint32_t data_buffer_head = 0;


    
    void initialize(uint32_t max_size);
    uint8_t *grow_data_buffer(uint32_t bytes);
    void send_serialized_message(network_address_t address);
    void receive_serialized_message(network_address_t address);
    
    // Basic serialization
    void serialize_uint8(uint8_t u8);
    void serialize_bytes(uint8_t *bytes, uint32_t size);
    void serialize_uint16(uint16_t u16);
    void serialize_uint32(uint32_t u32);
    void serialize_uint64(uint64_t u64);
    void serialize_float32(float32_t f32);
    void serialize_vector3(const vector3_t &v3);
    void serialize_string(const char *string);

    uint8_t deserialize_uint8(void);
    uint16_t deserialize_uint16(void);
    uint32_t deserialize_uint32(void);
    uint64_t deserialize_uint64(void);
    float32_t deserialize_float32(void);
    vector3_t deserialize_vector3(void);
    const char *deserialize_string(void);
    void deserialize_bytes(uint8_t *bytes, uint32_t size);

    // Complex serialization
    void serialize_packet_header(packet_header_t *packet);
    void deserialize_packet_header(packet_header_t *packet);
    void serialize_player_state_initialize_packet(player_state_initialize_packet_t *packet);
    void deserialize_player_state_initialize_packet(player_state_initialize_packet_t *packet);
    void serialize_voxel_state_initialize_packet(voxel_state_initialize_packet_t *packet);
    void deserialize_voxel_state_initialize_packet(voxel_state_initialize_packet_t *packet);
    void serialize_voxel_chunk_values_packet(voxel_chunk_values_packet_t *packet);
    void deserialize_voxel_chunk_values_packet(voxel_chunk_values_packet_t *packet);
    void serialize_game_state_initialize_packet(game_state_initialize_packet_t *packet);
    void deserialize_game_state_initialize_packet(game_state_initialize_packet_t *packet);
    void serialize_client_join_packet(client_join_packet_t *packet);
    void deserialize_client_join_packet(client_join_packet_t *packet);
    void serialize_client_input_state_packet(struct player_state_t *packet);
    void deserialize_client_input_state_packet(client_input_state_packet_t *packet);
    void serialize_game_snapshot_player_state_packet(game_snapshot_player_state_packet_t *packet);
    void deserialize_game_snapshot_player_state_packet(game_snapshot_player_state_packet_t *packet);
    void serialize_game_snapshot_voxel_delta_packet(game_snapshot_voxel_delta_packet_t *packet);
    void deserialize_game_snapshot_voxel_delta_packet(game_snapshot_voxel_delta_packet_t *packet, linear_allocator_t *allocator = nullptr);
    void serialize_client_modified_voxels_packet(client_modified_voxels_packet_t *packet);
    void deserialize_client_modified_voxels_packet(client_modified_voxels_packet_t *packet);    
};
