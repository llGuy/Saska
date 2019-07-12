#pragma once

#include "core.hpp"
#include "vulkan.hpp"
#include "graphics.hpp"
#include <glm/glm.hpp>
#include <glm/gtx/transform.hpp>

void
initialize_world(window_data_t *window
	   , vulkan_state_t *vk
	   , VkCommandPool *cmdpool);

void
update_world(window_data_t *window
	     , vulkan_state_t *vk
	     , float32_t dt
	     , uint32_t image_index
	     , uint32_t current_frame
	     , gpu_command_queue_t *queue);

void
handle_input_debug(window_data_t *win
                   , float32_t dt
                   , gpu_t *gpu);

void
destroy_world(gpu_t *gpu);
