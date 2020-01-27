/* core.hpp */

#pragma once

#include <cassert>
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>

#include "utility.hpp"
#include "memory.hpp"
#include <vulkan/vulkan.h>

#define DEBUG true

#define TICK_TIME 1.0f / 60.0f

#define MAX(x, y) (((x) > (y)) ? (x) : (y))
#define MIN(x, y) (((x) < (y)) ? (x) : (y))

#define ALLOCA_T(t, c) (t *)_malloca(sizeof(t) * c)
#define ALLOCA_B(s) _malloca(s)

#define VK_CHECK(f, ...) f

void request_quit(void);

float32_t get_dt(void);

struct create_vulkan_surface
{
    VkInstance *instance;
    VkSurfaceKHR *surface;

    virtual bool32_t create_proc(void) = 0;
};

inline constexpr uint32_t
left_shift(uint32_t n)
{
    return 1 << n;
}

void output_to_debug_console_i(float32_t f);
void output_to_debug_console_i(const vector3_t &v3);
void output_to_debug_console_i(const quaternion_t &q4);
void output_to_debug_console_i(int32_t i);
void output_to_debug_console_i(const char *string);

template <typename ...T> void output_to_debug_console(T &&...ts)
{
    // Could do this with C++17
    //(output_to_debug_console_i(ts), ...);

    // But in case C++17 is not supported:
    char dummy[] = { 0, (output_to_debug_console_i(ts), 0)... };
}


void print_text_to_console(const char *string);

template <typename T> inline void destroy(T *ptr, uint32_t size = 1)
{
    for (uint32_t i = 0; i < size; ++i)
    {
	ptr[i].~T();
    }
}



// predicate needs as param T &
/*template <typename T, typename Pred> void loop_through_memory(memory_buffer_view_t<T> &memory, Pred &&predicate)
{
    for (uint32_t i = 0; i < memory.count; ++i)
    {
	predicate(i);
    }
}*/

#include <glm/glm.hpp>

const matrix4_t IDENTITY_MAT4X4 = matrix4_t(1.0f);


void request_quit(void);


// TODO: Remove when not debugging
void enable_cursor_display(void);
void disable_cursor_display(void);
