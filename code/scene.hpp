#pragma once

#include "core.hpp"
#include "vulkan.hpp"
#include <glm/glm.hpp>
#include "rendering.hpp"
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

struct Scene
{
    Camera user_camera;

    VkCommandPool cmdpool;
    VkCommandBuffer cmdbuf;
    VkSemaphore rndr_finished;
    VkSemaphore img_ready;
    VkFence cpu_wait;
};

void
init_scene(Scene *scene
	   , Window_Data *window
	   , Vulkan_API::State *vk
	   , Rendering::Rendering_State *rnd);

void
update_scene(Scene *scene
	     , Window_Data *window
	     , Rendering::Rendering_State *rnd
	     , Vulkan_API::State *vk
	     , f32 dt);

void
destroy_scene(Scene *scene
	      , Vulkan_API::State *vk
	      , Rendering::Rendering_State *rnd);

void
handle_input(Scene *scene
	     ,Window_Data *win
	     , f32 dt);
