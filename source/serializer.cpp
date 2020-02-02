#include "gamestate.hpp"
#include "client.hpp"
#include "allocators.hpp"
#include "serializer.hpp"

#include "chunk.hpp"

void serializer_t::initialize(uint32_t max_size)
{
    data_buffer = (uint8_t *)allocate_linear(max_size * sizeof(uint8_t));
}

uint8_t *serializer_t::grow_data_buffer(uint32_t bytes)
{
    uint32_t previous = data_buffer_head;
    data_buffer_head += bytes;
    return(&data_buffer[previous]);
}

void serializer_t::send_serialized_message(network_address_t address)
{
    send_to(address,
            (char *)data_buffer,
            data_buffer_head);
}

void serializer_t::serialize_uint8(uint8_t u8)
{
    uint8_t *pointer = grow_data_buffer(1);
    *pointer = u8;
}

uint8_t serializer_t::deserialize_uint8(void)
{
    uint8_t *pointer = grow_data_buffer(1);
    return(*pointer);
}

void serializer_t::serialize_float32(float32_t f32)
{
    uint8_t *pointer = grow_data_buffer(4);
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

float32_t serializer_t::deserialize_float32(void)
{
    uint8_t *pointer = grow_data_buffer(4);
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

void serializer_t::serialize_vector3(const vector3_t &v3)
{
    serialize_float32(v3.x);
    serialize_float32(v3.y);
    serialize_float32(v3.z);
}

vector3_t serializer_t::deserialize_vector3(void)
{
    vector3_t v3 = {};
    v3.x = deserialize_float32();
    v3.y = deserialize_float32();
    v3.z = deserialize_float32();

    return(v3);
}

void serializer_t::serialize_uint16(uint16_t u16)
{
    uint8_t *pointer = grow_data_buffer(2);
#if defined (__i386) || defined (__x86_64__) || defined (_M_IX86) || defined(_M_X64)
    *(uint16_t *)pointer = u16;
#else
    *pointer++ = (uint8_t)u16;
    *pointer++ = (uint8_t)(u16 >> 8);
#endif
}

uint16_t serializer_t::deserialize_uint16(void)
{
    uint8_t *pointer = grow_data_buffer(2);
#if defined (__i386) || defined (__x86_64__) || defined (_M_IX86) || defined(_M_X64)
    return(*(uint16_t *)pointer);
#else
    uint16_t ret = 0;
    ret += (*pointer++);
    ret += ((uint16_t)(*pointer++)) << 8;
    return(ret);
#endif
}

void serializer_t::serialize_uint32(uint32_t u32)
{
    uint8_t *pointer = grow_data_buffer(4);
#if defined (__i386) || defined (__x86_64__) || defined (_M_IX86) || defined(_M_X64)
    *(uint32_t *)pointer = u32;
#else
    *pointer++ = (uint8_t)u32;
    *pointer++ = (uint8_t)(u32 >> 8);
    *pointer++ = (uint8_t)(u32 >> 16);
    *pointer++ = (uint8_t)(u32 >> 24);
#endif
}

uint32_t serializer_t::deserialize_uint32(void)
{
    uint8_t *pointer = grow_data_buffer(4);
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

void serializer_t::serialize_uint64(uint64_t u64)
{
    uint8_t *pointer = grow_data_buffer(8);
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

uint64_t serializer_t::deserialize_uint64(void)
{
    uint8_t *pointer = grow_data_buffer(8);
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

void serializer_t::serialize_string(const char *string)
{
    uint32_t string_length = strlen(string);
    
    uint8_t *pointer = grow_data_buffer(strlen(string) + 1);
    memcpy(pointer, string, string_length + 1);
}


const char *serializer_t::deserialize_string(void)
{
    uint8_t *pointer = &data_buffer[data_buffer_head];
    uint32_t string_length = strlen((char *)pointer);
    grow_data_buffer(string_length + 1);

    char *ret = (char *)allocate_free_list(string_length + 1);
    memcpy(ret, pointer, string_length + 1);
    return(ret);
}

void serializer_t::serialize_bytes(uint8_t *bytes, uint32_t size)
{
    uint8_t *pointer = grow_data_buffer(size);
    memcpy(pointer, bytes, size);
}


void serializer_t::deserialize_bytes(uint8_t *bytes, uint32_t size)
{
    uint8_t *pointer = grow_data_buffer(size);
    memcpy(bytes, pointer, size);
}


void serializer_t::serialize_packet_header(packet_header_t *packet)
{
    serialize_uint32(packet->bytes);
    serialize_uint64(packet->current_tick);
    serialize_uint64(packet->current_packet_id);
    serialize_uint32(packet->client_id);
}


void serializer_t::deserialize_packet_header(packet_header_t *packet)
{
    packet->bytes = deserialize_uint32();
    packet->current_tick = deserialize_uint64();
    packet->current_packet_id = deserialize_uint64();
    packet->client_id = deserialize_uint32();
}


void serializer_t::serialize_client_join_packet(client_join_packet_t *packet)
{
    serialize_string(packet->client_name);
}


void serializer_t::deserialize_client_join_packet(client_join_packet_t *packet)
{
    packet->client_name = deserialize_string();
}


void serializer_t::serialize_client_input_state_packet(player_state_t *state)
{
    serialize_uint32(state->action_flags);
    serialize_float32(state->mouse_x_diff);
    serialize_float32(state->mouse_y_diff);
    serialize_uint8(state->flags_byte);
    serialize_float32(state->dt);
    serialize_uint64(state->current_state_count);
}


void serializer_t::deserialize_client_input_state_packet(client_input_state_packet_t *packet)
{
    packet->action_flags = deserialize_uint32();
    packet->mouse_x_diff = deserialize_float32();
    packet->mouse_y_diff = deserialize_float32();
    packet->flags_byte = deserialize_uint8();
    packet->dt = deserialize_float32();
    packet->command_id = deserialize_uint64();
}


void serializer_t::serialize_player_state_initialize_packet(player_state_initialize_packet_t *packet)
{
    serialize_uint32(packet->client_id);
    serialize_string(packet->player_name);
    
    serialize_float32(packet->ws_position_x);
    serialize_float32(packet->ws_position_y);
    serialize_float32(packet->ws_position_z);

    serialize_float32(packet->ws_view_direction_x);
    serialize_float32(packet->ws_view_direction_y);
    serialize_float32(packet->ws_view_direction_z);

    // Etc...
}


void serializer_t::deserialize_player_state_initialize_packet(player_state_initialize_packet_t *packet)
{
    packet->client_id = deserialize_uint32();
    packet->player_name = deserialize_string();
    
    packet->ws_position_x = deserialize_float32();
    packet->ws_position_y = deserialize_float32();
    packet->ws_position_z = deserialize_float32();

    packet->ws_view_direction_x = deserialize_float32();
    packet->ws_view_direction_y = deserialize_float32();
    packet->ws_view_direction_z = deserialize_float32();
    // Etc...
}


void serializer_t::serialize_voxel_state_initialize_packet(voxel_state_initialize_packet_t *packet)
{
    serialize_uint32(packet->grid_edge_size);
    serialize_float32(packet->size);
    serialize_uint32(packet->chunk_count);
    serialize_uint32(packet->max_chunks);
}


void serializer_t::deserialize_voxel_state_initialize_packet(voxel_state_initialize_packet_t *packet)
{
    packet->grid_edge_size = deserialize_uint32();
    packet->size = deserialize_float32();
    packet->chunk_count = deserialize_uint32();
    packet->max_chunks = deserialize_uint32();
}


void serializer_t::serialize_voxel_chunk_values_packet(voxel_chunk_values_packet_t *packet)
{
    serialize_uint8(packet->chunk_coord_x);
    serialize_uint8(packet->chunk_coord_y);
    serialize_uint8(packet->chunk_coord_z);
    serialize_bytes(packet->voxels, sizeof(uint8_t) * CHUNK_EDGE_LENGTH * CHUNK_EDGE_LENGTH * CHUNK_EDGE_LENGTH);
}


void serializer_t::deserialize_voxel_chunk_values_packet(voxel_chunk_values_packet_t *packet)
{
    packet->chunk_coord_x = deserialize_uint8();
    packet->chunk_coord_y = deserialize_uint8();
    packet->chunk_coord_z = deserialize_uint8();

    packet->voxels = (uint8_t *)allocate_linear(sizeof(uint8_t) * CHUNK_EDGE_LENGTH * CHUNK_EDGE_LENGTH * CHUNK_EDGE_LENGTH);
    deserialize_bytes(packet->voxels, sizeof(uint8_t) * CHUNK_EDGE_LENGTH * CHUNK_EDGE_LENGTH * CHUNK_EDGE_LENGTH);
}


void serializer_t::serialize_game_state_initialize_packet(game_state_initialize_packet_t *packet)
{
    serialize_voxel_state_initialize_packet(&packet->voxels);
    serialize_uint32(packet->client_index);
    serialize_uint32(packet->player_count);
    for (uint32_t i = 0; i < packet->player_count; ++i)
    {
        serialize_player_state_initialize_packet(&packet->player[i]);
    }
}


void serializer_t::serialize_game_snapshot_player_state_packet(game_snapshot_player_state_packet_t *packet)
{
    serialize_uint32(packet->client_id);
    
    serialize_float32(packet->ws_position.x);
    serialize_float32(packet->ws_position.y);
    serialize_float32(packet->ws_position.z);

    serialize_float32(packet->ws_direction.x);
    serialize_float32(packet->ws_direction.y);
    serialize_float32(packet->ws_direction.z);

    serialize_float32(packet->ws_velocity.x);
    serialize_float32(packet->ws_velocity.y);
    serialize_float32(packet->ws_velocity.z);

    serialize_float32(packet->ws_previous_velocity.x);
    serialize_float32(packet->ws_previous_velocity.y);
    serialize_float32(packet->ws_previous_velocity.z);
    
    serialize_float32(packet->ws_up_vector.x);
    serialize_float32(packet->ws_up_vector.y);
    serialize_float32(packet->ws_up_vector.z);

    serialize_float32(packet->ws_rotation[0]);
    serialize_float32(packet->ws_rotation[1]);
    serialize_float32(packet->ws_rotation[2]);
    serialize_float32(packet->ws_rotation[3]);

    serialize_uint32(packet->action_flags);

    serialize_uint8(packet->flags);
}


void serializer_t::deserialize_game_snapshot_player_state_packet(game_snapshot_player_state_packet_t *packet)
{
    packet->client_id = deserialize_uint32();
    
    packet->ws_position.x = deserialize_float32();
    packet->ws_position.y = deserialize_float32();
    packet->ws_position.z = deserialize_float32();

    packet->ws_direction.x = deserialize_float32();
    packet->ws_direction.y = deserialize_float32();
    packet->ws_direction.z = deserialize_float32();

    packet->ws_velocity.x = deserialize_float32();
    packet->ws_velocity.y = deserialize_float32();
    packet->ws_velocity.z = deserialize_float32();

    packet->ws_previous_velocity.x = deserialize_float32();
    packet->ws_previous_velocity.y = deserialize_float32();
    packet->ws_previous_velocity.z = deserialize_float32();

    packet->ws_up_vector.x = deserialize_float32();
    packet->ws_up_vector.y = deserialize_float32();
    packet->ws_up_vector.z = deserialize_float32();

    packet->ws_rotation[0] = deserialize_float32();
    packet->ws_rotation[1] = deserialize_float32();
    packet->ws_rotation[2] = deserialize_float32();
    packet->ws_rotation[3] = deserialize_float32();

    packet->action_flags = deserialize_uint32();

    packet->flags = deserialize_uint8();
}


void serializer_t::serialize_client_modified_voxels_packet(client_modified_voxels_packet_t *packet)
{
    serialize_uint32(packet->modified_chunk_count);

    for (uint32_t chunk = 0; chunk < packet->modified_chunk_count; ++chunk)
    {
        serialize_uint16(packet->modified_chunks[chunk].chunk_index);
        serialize_uint32(packet->modified_chunks[chunk].modified_voxel_count);
        for (uint32_t voxel = 0; voxel < packet->modified_chunks[chunk].modified_voxel_count; ++voxel)
        {
            serialize_uint8(packet->modified_chunks[chunk].modified_voxels[voxel].x);
            serialize_uint8(packet->modified_chunks[chunk].modified_voxels[voxel].y);
            serialize_uint8(packet->modified_chunks[chunk].modified_voxels[voxel].z);
            serialize_uint8(packet->modified_chunks[chunk].modified_voxels[voxel].value);
        }
    }
}


void serializer_t::deserialize_client_modified_voxels_packet(client_modified_voxels_packet_t *packet)
{
    packet->modified_chunk_count = deserialize_uint32();

    packet->modified_chunks = (client_modified_chunk_t *)allocate_linear(sizeof(client_modified_chunk_t) * packet->modified_chunk_count);

    for (uint32_t chunk = 0; chunk < packet->modified_chunk_count; ++chunk)
    {
        packet->modified_chunks[chunk].chunk_index = deserialize_uint16();

        packet->modified_chunks[chunk].modified_voxel_count = deserialize_uint32();
        
        packet->modified_chunks[chunk].modified_voxels = (local_client_modified_voxel_t *)allocate_linear(sizeof(local_client_modified_voxel_t) * packet->modified_chunks[chunk].modified_voxel_count);
        for (uint32_t voxel = 0; voxel < packet->modified_chunks[chunk].modified_voxel_count; ++voxel)
        {
            packet->modified_chunks[chunk].modified_voxels[voxel].x = deserialize_uint8();
            packet->modified_chunks[chunk].modified_voxels[voxel].y = deserialize_uint8();
            packet->modified_chunks[chunk].modified_voxels[voxel].z = deserialize_uint8();
            packet->modified_chunks[chunk].modified_voxels[voxel].value = deserialize_uint8();
        }
    }
}


void serializer_t::serialize_game_snapshot_voxel_delta_packet(game_snapshot_voxel_delta_packet_t *packet)
{
    serialize_uint32(packet->modified_count);

    for (uint32_t chunk = 0; chunk < packet->modified_count; ++chunk)
    {
        serialize_uint16(packet->modified_chunks[chunk].chunk_index);
        serialize_uint32(packet->modified_chunks[chunk].modified_voxel_count);
        for (uint32_t voxel = 0; voxel < packet->modified_chunks[chunk].modified_voxel_count; ++voxel)
        {
            serialize_uint8(packet->modified_chunks[chunk].modified_voxels[voxel].previous_value);
            serialize_uint8(packet->modified_chunks[chunk].modified_voxels[voxel].next_value);
            serialize_uint16(packet->modified_chunks[chunk].modified_voxels[voxel].index);
        }
    }
}


void serializer_t::deserialize_game_snapshot_voxel_delta_packet(game_snapshot_voxel_delta_packet_t *packet, linear_allocator_t *allocator)
{
    packet->modified_count = deserialize_uint32();

    packet->modified_chunks = (modified_chunk_t *)allocate_linear(sizeof(modified_chunk_t) * packet->modified_count, 1, "", allocator);

    uint32_t output_count = 0;
    
    for (uint32_t chunk = 0; chunk < packet->modified_count; ++chunk)
    {
        packet->modified_chunks[chunk].chunk_index = deserialize_uint16();

        packet->modified_chunks[chunk].modified_voxel_count = deserialize_uint32();
        
        packet->modified_chunks[chunk].modified_voxels = (modified_voxel_t *)allocate_linear(sizeof(modified_voxel_t) * packet->modified_chunks[chunk].modified_voxel_count, 1, "", allocator);
        for (uint32_t voxel = 0; voxel < packet->modified_chunks[chunk].modified_voxel_count; ++voxel)
        {
            packet->modified_chunks[chunk].modified_voxels[voxel].previous_value = deserialize_uint8();
            packet->modified_chunks[chunk].modified_voxels[voxel].next_value = deserialize_uint8();
            packet->modified_chunks[chunk].modified_voxels[voxel].index = deserialize_uint16();

            ++output_count;
            output_to_debug_console((int32_t)(packet->modified_chunks[chunk].modified_voxels[voxel].next_value), " ");
        }
    }

    if (output_count)
    {
        output_to_debug_console("\n");
    }
}


void serializer_t::deserialize_game_state_initialize_packet(game_state_initialize_packet_t *packet)
{
    deserialize_voxel_state_initialize_packet(&packet->voxels);
    packet->client_index = deserialize_uint32();
    packet->player_count = deserialize_uint32();
    packet->player = (player_state_initialize_packet_t *)allocate_linear(sizeof(player_state_initialize_packet_t) * packet->player_count);
    for (uint32_t i = 0; i < packet->player_count; ++i)
    {
        deserialize_player_state_initialize_packet(&packet->player[i]);
    }
}
