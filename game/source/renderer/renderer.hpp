#pragma once

#include <stdint.h>

enum g3d_api_flags_t { SUPPORTS_SUBPASS = 1 << 0,
                   SUPPORTS_PUSH_CONSTANT = 1 << 1 /* ... */};

extern uint64_t g3d_api_flag_bits;

// --- User's code uses these
typedef uint32_t g3d_buffer_t;
typedef uint32_t g3d_pipeline_t;
typedef uint32_t g3d_texture_t;

typedef uint32_t g3d_uniform_layout_t;
typedef uint32_t g3d_uniform_group_t;

typedef uint32_t g3d_render_pass_t;
typedef uint32_t g3d_framebuffer_t;

typedef uint32_t g3d_command_buffer_t;
// ---

typedef void(*g3d_debug_callback_t)(const char *message);

struct g3d_display_window_info_t
{
    uint32_t width, height;
};

// TODO: Pass to this the linear, stack and free list allocators so that module can use it!
// platform_specific:
// - Win32: HWND *
void g3d_initialize_renderer(void *platform_specific, g3d_debug_callback_t debug_callback, g3d_display_window_info_t window_info);
void g3d_begin_frame_rendering(void);
void g3d_end_frame_rendering_and_refresh(void);

g3d_command_buffer_t g3d_create_command_buffer(void);

void g3d_destroy_command_buffer(void);
