#pragma once

#include "utils.hpp"

struct serializer_t
{
    uint32_t data_buffer_size;
    uint8_t *data_buffer;
    uint32_t data_buffer_head = 0;
};

void initialize_serializer(serializer_t *serializer, uint32_t max_size);
uint8_t *grow_serializer_data_buffer(serializer_t *serializer, uint32_t bytes);
void send_serialized_message(serializer_t *serializer, network_address_t address);


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
