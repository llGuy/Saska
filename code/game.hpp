#pragma once

#include "vulkan.hpp"

void
make_game(vulkan_state_t *vk
	  , gpu_t *gpu
	  , Swapchain *swapchain
	  , window_data_t *window);

void
destroy_game(gpu_t *gpu);

void
update_game(gpu_t *gpu
	    , swapchain_t *swapchain
	    , window_data_t *window
	    , vulkan_state_t *vk
	    , float32_t dt);


