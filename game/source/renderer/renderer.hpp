#pragma once

#include <stdint.h>

enum api_flags_t { SUPPORTS_SUBPASS = 1 << 0,
                   SUPPORTS_PUSH_CONSTANT = 1 << 1 /* ... */};

extern uint64_t api_flag_bits;

typedef uint32_t gpu_buffer_t;
typedef uint32_t gpu_pipeline_t;
typedef uint32_t gpu_texture_t;

typedef uint32_t uniform_layout_t;
typedef uint32_t uniform_group_t;

typedef uint32_t render_pass_t;
typedef uint32_t framebuffer_t;

#define DECLARE_RENDERER_METHOD(return_type, name, ...) extern return_type(*name)(__VA_ARGS__)

typedef void(*debug_callback_t)(const char *message);


struct allocators_t
{
    struct linear_allocator_t *linear_allocator;
    struct stack_allocator_t *stack_allocator;
    struct free_list_allocator_t *free_list_allocator;
};

struct display_window_info_t
{
    uint32_t width, height;
};

// TODO: Pass to this the linear, stack and free list allocators so that module can use it!
// platform_specific:
// - Win32: HWND *
DECLARE_RENDERER_METHOD(void, initialize_renderer, void *platform_specific, debug_callback_t debug_callback, allocators_t allocators, display_window_info_t window_info);
