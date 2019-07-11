#pragma once

#include "vulkan.hpp"

void
initialize_game_ui(GPU *gpu, GPU_Command_Queue_Pool *qpool, Swapchain *swapchain, Uniform_Pool *uniform_pool, const Resolution &);

void
update_game_ui(GPU *gpu, Framebuffer_Handle dst_framebuffer_hdl);

void
render_game_ui(GPU *gpu, Framebuffer_Handle dst_framebuffer_hdl, GPU_Command_Queue *queue);
