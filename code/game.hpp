#pragma once

#include "vulkan.hpp"

void
make_game(Vulkan_State *vk
	  , GPU *gpu
	  , Swapchain *swapchain
	  , Window_Data *window);

void
destroy_game(GPU *gpu);

void
update_game(GPU *gpu
	    , Swapchain *swapchain
	    , Window_Data *window
	    , Vulkan_State *vk
	    , f32 dt);


