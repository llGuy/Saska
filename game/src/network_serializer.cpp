#include "network.hpp"
#include "network_serializer.hpp"


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
    send_to(address,
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
