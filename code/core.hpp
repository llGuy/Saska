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

inline constexpr uint32_t
left_shift(uint32_t n)
{
    return 1 << n;
}

void
output_debug(const char *format
	     , ...);

struct window_data_t
{
    int32_t w, h;
    struct GLFWwindow *window;
    float32_t dt = 0.0f;

    memory_buffer_view_t<bool> key_map;
    memory_buffer_view_t<bool> mb_map;

    // Mouse position
    int32_t m_x = 0;
    int32_t m_y = 0;

    // Previous mouse position
    int32_t prev_m_x = 0;
    int32_t prev_m_y;

    bool m_moved = false;
    bool window_resized = false;
};



struct file_contents_t
{
    uint32_t size;
    byte_t *content;
};

file_contents_t
read_file(const char *filename,
          const char *flags = "rb",
          stack_allocator_t *allocator = &stack_allocator_global);

struct external_image_data_t
{
    int32_t width;
    int32_t height;
    void *pixels;
    int32_t channels;
};

external_image_data_t
read_image(const char *filename);

template <typename T>
inline void
destroy(T *ptr
	, uint32_t size = 1)
{
    for (uint32_t i = 0
	     ; i < size
	     ; ++i)
    {
	ptr[i].~T();
    }
}

#ifndef __GNUC__
#include <intrin.h>
#endif

struct bitset32_t
{
    uint32_t bitset = 0;

    inline uint32_t
    pop_count(void)
    {
#ifndef __GNUC__
	return __popcnt(bitset);
#else
	return __builtin_popcount(bitset);  
#endif
    }

    inline void
    set1(uint32_t bit)
    {
	bitset |= left_shift(bit);
    }

    inline void
    set0(uint32_t bit)
    {
	bitset &= ~(left_shift(bit));
    }

    inline bool
    get(uint32_t bit)
    {
	return bitset & left_shift(bit);
    }
};

template <typename T> internal constexpr memory_buffer_view_t<T>
null_buffer(void) {return(memory_buffer_view_t<T>{0, nullptr});}

template <typename T> internal constexpr memory_buffer_view_t<T>
single_buffer(T *address) {return(memory_buffer_view_t<T>{1, address});}

template <typename T> void
allocate_memory_buffer(memory_buffer_view_t<T> &view, uint32_t count)
{
    view.count = count;
    view.buffer = (T *)allocate_free_list(count * sizeof(T));
}

template <typename T> void
allocate_memory_buffer_tmp(memory_buffer_view_t<T> &view, uint32_t count)
{
    view.count = count;
    view.buffer = (T *)allocate_linear(count * sizeof(T));
}

struct memory_byte_buffer_t
{
    uint32_t size;
    void *ptr;
};

// predicate needs as param T &
template <typename T, typename Pred> void
loop_through_memory(memory_buffer_view_t<T> &memory
		    , Pred &&predicate)
{
    for (uint32_t i = 0; i < memory.count; ++i)
    {
	predicate(i);
    }
}

#include <glm/glm.hpp>

const matrix4_t IDENTITY_MAT4X4 = matrix4_t(1.0f);

extern float32_t
barry_centric(const vector3_t &p1, const vector3_t &p2, const vector3_t &p3, const vector2_t &pos);
