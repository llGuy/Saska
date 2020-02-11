#pragma once

#include "graphics.hpp"

void initialize_deferred_renderer();
void begin_deferred_rendering(gpu_command_queue_t *queue);
void do_lighting_and_begin_alpha_rendering(sun_t *sun, const matrix4_t &view_matrix, gpu_command_queue_t *queue);
void end_deferred_rendering(gpu_command_queue_t *queue);

resolution_t backbuffer_resolution();
render_pass_t *deferred_render_pass();
uniform_group_t *gbuffer_uniform();