#pragma once

#include "core.hpp"
#include "vulkan.hpp"
#include "graphics.hpp"
#include <glm/glm.hpp>
#include <glm/gtx/transform.hpp>

void initialize_world(input_state_t *input_state, VkCommandPool *cmdpool);

void update_world(input_state_t *input_state, float32_t dt, uint32_t image_index,
                  uint32_t current_frame, gpu_command_queue_t *queue);

void handle_input_debug(input_state_t *input_state, float32_t dt);

void destroy_world(void);
