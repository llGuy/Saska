#pragma once

#include "core.hpp"
#include "vulkan.hpp"
#include <glm/glm.hpp>
#include <glm/gtx/transform.hpp>

struct Camera
{
    glm::vec2 mp;
    glm::vec3 p; // position
    glm::vec3 d; // direction
    glm::vec3 u; // up

    f32 fov;
    f32 asp; // aspect ratio
    f32 n, f; // near and far planes

    glm::mat4 p_m;
    glm::mat4 v_m;

    void
    set_default(f32 w, f32 h, f32 m_x, f32 m_y);
    
    void
    compute_projection(void);

    void
    compute_view(void);
};

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
handle_input(Window_Data *win
	     , f32 dt
	     , Vulkan::GPU *gpu);
