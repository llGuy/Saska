#pragma once

#include <cassert>
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>

#include "utils.hpp"
#include "memory.hpp"

#define DEBUG true

#define MAX(x, y) (((x) > (y)) ? (x) : (y))
#define MIN(x, y) (((x) < (y)) ? (x) : (y))

#define ALLOCA_T(t, c) (t *)alloca(sizeof(t) * c)
#define ALLOCA_B(s) alloca(s)

#define VK_CHECK(f, ...) \
        if (f != VK_SUCCESS)			\
    { \
	fprintf(output_file.fp, "[%s:%d] error : %s - ", __FILE__, __LINE__, #f); \
     assert(false);	  \
     } 

inline constexpr u32
left_shift(u32 n)
{
    return 1 << n;
}

void
output_debug(const char *format
	     , ...);

struct Window_Data
{
    s32 w, h;
    struct GLFWwindow *window;
    f32 dt = 0.0f;

    Memory_Buffer_View<bool> key_map;
    Memory_Buffer_View<bool> mb_map;

    // Mouse position
    s32 m_x = 0;
    s32 m_y = 0;

    // Previous mouse position
    s32 prev_m_x = 0;
    s32 prev_m_y;

    bool m_moved = false;
    bool window_resized = false;
};



struct File_Contents
{
    u32 size;
    byte *content;
};

internal File_Contents
read_file(const char *filename
	  , const char *flags = "rb"
	  , Stack_Allocator *allocator = &stack_allocator_global)
{
    FILE *file = fopen(filename, flags);
    if (file == nullptr)
    {
	OUTPUT_DEBUG_LOG("error - couldnt load file \"%s\"\n", filename);
	assert(false);
    }
    fseek(file, 0, SEEK_END);
    u32 size = ftell(file);
    rewind(file);

    byte *buffer = (byte *)allocate_stack(size + 1
					  , 1
					  , filename
					  , allocator);
    fread(buffer, 1, size, file);

    buffer[size] = '\0';
    
    fclose(file);

    File_Contents contents { size, buffer };
    
    return(contents);
} 

template <typename T>
inline void
destroy(T *ptr
	, u32 size = 1)
{
    for (u32 i = 0
	     ; i < size
	     ; ++i)
    {
	ptr[i].~T();
    }
}

#ifndef __GNUC__
#include <intrin.h>
#endif

struct Bitset_32
{
    u32 bitset = 0;

    inline u32
    pop_count(void)
    {
#ifndef __GNUC__
	return __popcnt(bitset);
#else
	return __builtin_popcount(bitset);  
#endif
    }

    inline void
    set1(u32 bit)
    {
	bitset |= left_shift(bit);
    }

    inline void
    set0(u32 bit)
    {
	bitset &= ~(left_shift(bit));
    }

    inline bool
    get(u32 bit)
    {
	return bitset & left_shift(bit);
    }
};

template <typename T> internal constexpr Memory_Buffer_View<T>
null_buffer(void) {return(Memory_Buffer_View<T>{0, nullptr});}

template <typename T> internal constexpr Memory_Buffer_View<T>
single_buffer(T *address) {return(Memory_Buffer_View<T>{1, address});}

template <typename T> void
allocate_memory_buffer(Memory_Buffer_View<T> &view, u32 count)
{
    view.count = count;
    view.buffer = (T *)allocate_free_list(count * sizeof(T));
}

template <typename T> void
allocate_memory_buffer_tmp(Memory_Buffer_View<T> &view, u32 count)
{
    view.count = count;
    view.buffer = (T *)allocate_linear(count * sizeof(T));
}

struct Memory_Byte_Buffer
{
    u32 size;
    void *ptr;
};

// predicate needs as param T &
template <typename T, typename Pred> void
loop_through_memory(Memory_Buffer_View<T> &memory
		    , Pred &&predicate)
{
    for (u32 i = 0; i < memory.count; ++i)
    {
	predicate(i);
    }
}

#include <glm/glm.hpp>

const m4x4 IDENTITY_MAT4X4 = m4x4(1.0f);

extern f32
barry_centric(const v3 &p1, const v3 &p2, const v3 &p3, const v2 &pos);
