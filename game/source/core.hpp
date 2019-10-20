/* core.hpp */

#pragma once

#include <cassert>
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>

#include "utils.hpp"
#include "memory.hpp"
#include <vulkan/vulkan.h>

#define DEBUG true

#define TICK_TIME 1.0f / 20.0f

#define MAX(x, y) (((x) > (y)) ? (x) : (y))
#define MIN(x, y) (((x) < (y)) ? (x) : (y))

#define ALLOCA_T(t, c) (t *)alloca(sizeof(t) * c)
#define ALLOCA_B(s) alloca(s)

#define VK_CHECK(f, ...) f

enum platform_t { AGNOSTIC, WINDOWS, LINUX, INVALID };
extern const platform_t PLATFORM = INVALID;

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

void output_debug_string(const char *string);
void print_text_to_console(const char *string);

template <typename T> inline void destroy(T *ptr, uint32_t size = 1)
{
    for (uint32_t i = 0; i < size; ++i)
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

    inline uint32_t pop_count(void)
    {
#ifndef __GNUC__
	return __popcnt(bitset);
#else
	return __builtin_popcount(bitset);  
#endif
    }

    inline void set1(uint32_t bit)
    {
	bitset |= left_shift(bit);
    }

    inline void set0(uint32_t bit)
    {
	bitset &= ~(left_shift(bit));
    }

    inline bool get(uint32_t bit)
    {
	return bitset & left_shift(bit);
    }
};

template <typename T> internal_function constexpr memory_buffer_view_t<T> null_buffer(void) {return(memory_buffer_view_t<T>{0, nullptr});}
template <typename T> internal_function constexpr memory_buffer_view_t<T> single_buffer(T *address) {return(memory_buffer_view_t<T>{1, address});}
template <typename T> void allocate_memory_buffer(memory_buffer_view_t<T> &view, uint32_t count)
{
    view.count = count;
    view.buffer = (T *)allocate_free_list(count * sizeof(T));
}

template <typename T> void allocate_memory_buffer_tmp(memory_buffer_view_t<T> &view, uint32_t count)
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
template <typename T, typename Pred> void loop_through_memory(memory_buffer_view_t<T> &memory, Pred &&predicate)
{
    for (uint32_t i = 0; i < memory.count; ++i)
    {
	predicate(i);
    }
}

#include <glm/glm.hpp>

const matrix4_t IDENTITY_MAT4X4 = matrix4_t(1.0f);

extern float32_t barry_centric(const vector3_t &p1, const vector3_t &p2, const vector3_t &p3, const vector2_t &pos);

enum keyboard_button_type_t { A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
                              ZERO, ONE, TWO, THREE, FOUR, FIVE, SIX, SEVEN, EIGHT, NINE, UP, LEFT, DOWN, RIGHT,
                              SPACE, LEFT_SHIFT, LEFT_CONTROL, ENTER, BACKSPACE, ESCAPE, INVALID_KEY };

enum is_down_t : bool { NOT_DOWN, INSTANT, REPEAT };

struct keyboard_button_input_t
{
    is_down_t is_down = is_down_t::NOT_DOWN;
    float32_t down_amount = 0.0f;
};

enum mouse_button_type_t { MOUSE_LEFT, MOUSE_RIGHT, MOUSE_MIDDLE, INVALID_MOUSE_BUTTON };

struct mouse_button_input_t
{
    is_down_t is_down = is_down_t::NOT_DOWN;
    float32_t down_amount = 0.0f;
};

#define MAX_CHARS 10

struct input_state_t
{
    keyboard_button_input_t keyboard[keyboard_button_type_t::INVALID_KEY];
    mouse_button_input_t mouse_buttons[mouse_button_type_t::INVALID_MOUSE_BUTTON];

    uint32_t char_count;
    char char_stack[10] = {};

    bool cursor_moved = 0;
    float32_t cursor_pos_x = 0.0f, cursor_pos_y = 0.0f;
    float32_t previous_cursor_pos_x = 0.0f, previous_cursor_pos_y = 0.0f;

    bool resized = 0;
    int32_t window_width, window_height;

    vector2_t normalized_cursor_position;

    float32_t dt;
};

// TODO: Remove when not debugging
input_state_t *get_input_state(void);
void enable_cursor_display(void);
void disable_cursor_display(void);

template <typename T, uint32_t Max> struct stack_dynamic_container_t
{
    uint32_t data_count = 0;
    T data[Max];

    uint32_t removed_count = 0;
    uint32_t removed[Max];

    uint32_t add(void)
    {
        if (removed_count)
        {
            return removed[removed_count-- - 1];
        }
        else
        {
            return data_count++;
        }
    }

    T *get(uint32_t index)
    {
        return &data[index];
    }

    void remove(uint32_t index)
    {
        removed[removed_count++] = index;
    }
};
