#pragma once

#include "core.hpp"
#include "vulkan.hpp"
#include "graphics.hpp"
#include <glm/glm.hpp>
#include <glm/gtx/transform.hpp>

void
initialize_world(Window_Data *window
	   , Vulkan_State *vk
	   , VkCommandPool *cmdpool);

void
update_world(Window_Data *window
	     , Vulkan_State *vk
	     , f32 dt
	     , u32 image_index
	     , u32 current_frame
	     , GPU_Command_Queue *queue);

void
handle_input_debug(Window_Data *win
                   , f32 dt
                   , GPU *gpu);

void
destroy_world(GPU *gpu);
