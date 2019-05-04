#pragma once

#include "vulkan.hpp"
#include "rendering.hpp"

void
make_game(Vulkan::State *vk
	  , Vulkan::GPU *gpu
	  , Vulkan::Swapchain *swapchain
	  , Window_Data *window
	  , Rendering::Rendering_State *cache /* to get rid of in the future */);

void
destroy_game(Vulkan::GPU *gpu);

void
update_game(Vulkan::GPU *gpu
	    , Vulkan::Swapchain *swapchain
	    , Window_Data *window
	    , Rendering::Rendering_State *rnd
	    , Vulkan::State *vk
	    , f32 dt);


