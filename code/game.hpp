#pragma once

#include "vulkan.hpp"

void
make_game(input_state_t *input_state);

void
destroy_game(gpu_t *gpu);

void
update_game(gpu_t *gpu
	    , swapchain_t *swapchain
	    , input_state_t *input_state
	    , vulkan_state_t *vk
	    , float32_t dt);


