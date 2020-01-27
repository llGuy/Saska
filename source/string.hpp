#pragma once

#include "utility.hpp"

struct constant_string_t
{
    const char* str;
    uint32_t size;
    uint32_t hash;

    inline bool
    operator==(const constant_string_t &other) {return(other.hash == this->hash);}
};

inline constexpr uint32_t compile_hash(const char *string, uint32_t size)
{
    return ((size ? compile_hash(string, size - 1) : 2166136261u) ^ string[size]) * 16777619u;
}

inline constexpr constant_string_t operator""_hash(const char *string, size_t size) // "
{
    return(constant_string_t{string, (uint32_t)size, compile_hash(string, (uint32_t)size)});
}

inline constant_string_t make_constant_string(const char *str, uint32_t count)
{
    return(constant_string_t{str, count, compile_hash(str, count)});
}

inline constant_string_t init_const_str(const char *str, uint32_t count)
{
    return(constant_string_t{str, count, compile_hash(str, count)});
}
