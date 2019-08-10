#pragma once

#include "vulkan.hpp"

void
initialize_game_ui(gpu_command_queue_pool_t *qpool, uniform_pool_t *uniform_pool, const resolution_t &);

void
update_game_ui(framebuffer_handle_t dst_framebuffer_hdl, input_state_t *input_state);

void
render_game_ui(framebuffer_handle_t dst_framebuffer_hdl, gpu_command_queue_t *queue);

void
load_font(const constant_string_t &font_name, const char *fmt_file);

bool
console_is_receiving_input(void);

void
console_out(const char *string);

void
console_out_color_override(const char *string, uint32_t color);

uint32_t
vec4_color_to_ui32b(const vector4_t &color);
