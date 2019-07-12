#pragma once

#include "vulkan.hpp"

void
initialize_game_ui(gpu_t *gpu, gpu_command_queue_pool_t *qpool, swapchain_t *swapchain, uniform_pool_t *uniform_pool, const resolution_t &);

void
update_game_ui(gpu_t *gpu, framebuffer_handle_t dst_framebuffer_hdl);

void
render_game_ui(gpu_t *gpu, framebuffer_handle_t dst_framebuffer_hdl, gpu_command_queue_t *queue);
