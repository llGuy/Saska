#pragma once

#include "core.hpp"
#include "vulkan.hpp"
#include <glm/glm.hpp>
#include <glm/gtx/transform.hpp>

void
make_world(Window_Data *window
	   , Vulkan::State *vk
	   , VkCommandPool *cmdpool);

void
update_world(Window_Data *window
	     , Vulkan::State *vk
	     , f32 dt
	     , u32 image_index
	     , u32 current_frame
	     , VkCommandBuffer *cmdbuf);

void
handle_input_debug(Window_Data *win
                   , f32 dt
                   , Vulkan::GPU *gpu);

void
destroy_world(Vulkan::GPU *gpu);
