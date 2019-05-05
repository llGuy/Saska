#pragma once

#include "vulkan.hpp"
#include "rendering.hpp"

void
make_game(Vulkan::State *vk
	  , Vulkan::GPU *gpu
	  , Vulkan::Swapchain *swapchain
	  , Window_Data *window);

void
destroy_game(Vulkan::GPU *gpu);

void
update_game(Vulkan::GPU *gpu
	    , Vulkan::Swapchain *swapchain
	    , Window_Data *window
	    , Vulkan::State *vk
	    , f32 dt);


