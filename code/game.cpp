#include "world.hpp"
#include "vulkan.hpp"
#include "graphics.hpp"

#include "script.hpp"
#include "ui.hpp"

global_var struct window_rendering_data_t
{
    // ---- sync objects ----
    VkSemaphore img_ready [2];
    VkSemaphore render_finish [2];
    VkFence cpu_wait [2];

    // ---- command buffer stuff ----
    VkCommandPool command_pool;
    VkCommandBuffer command_buffer[3];

    // ---- data needed to render ----
    uint32_t image_index;
    uint32_t in_flight_fram;
} window_rendering;

void
make_game(vulkan_state_t *vk, gpu_t *gpu, swapchain_t *swapchain, window_data_t *window)
{
    allocate_command_pool(gpu->queue_families.graphics_family
				  , gpu
				  , &window_rendering.command_pool);

    allocate_command_buffers(&window_rendering.command_pool
				     , VK_COMMAND_BUFFER_LEVEL_PRIMARY
				     , gpu
				     , memory_buffer_view_t<VkCommandBuffer>{3, window_rendering.command_buffer});

    for (uint32_t i = 0; i < 2; ++i)
    {
        init_semaphore(gpu, &window_rendering.img_ready[i]);
        init_semaphore(gpu, &window_rendering.render_finish[i]);   
        init_fence(gpu, VK_FENCE_CREATE_SIGNALED_BIT, &window_rendering.cpu_wait[i]);
    }
    

    // ---- Initialize game data ----
    // Initialize atmosphere, shadow, skeletal animation...
    initialize_game_3d_graphics(gpu, swapchain, &window_rendering.command_pool);
    initialize_game_2d_graphics(gpu, swapchain, &window_rendering.command_pool);
    initialize_game_ui(gpu, &window_rendering.command_pool, &vk->swapchain, &g_uniform_pool, get_backbuffer_resolution());
    initialize_world(window, vk, &window_rendering.command_pool);

    make_lua_scripting();

    test_script();
}

void
destroy_game(gpu_t *gpu)
{
    // ---- destroy world data ----
    destroy_world(gpu);
    
    free_command_buffer(memory_buffer_view_t<VkCommandBuffer>{3, window_rendering.command_buffer}
				, &window_rendering.command_pool, gpu);

    vkDestroyCommandPool(gpu->logical_device, window_rendering.command_pool, nullptr);
    vkDestroyFence(gpu->logical_device, window_rendering.cpu_wait[0], nullptr);
    vkDestroyFence(gpu->logical_device, window_rendering.cpu_wait[1], nullptr);
    
    vkDestroySemaphore(gpu->logical_device, window_rendering.render_finish[0], nullptr);
    vkDestroySemaphore(gpu->logical_device, window_rendering.img_ready[0], nullptr);

    vkDestroySemaphore(gpu->logical_device, window_rendering.render_finish[1], nullptr);
    vkDestroySemaphore(gpu->logical_device, window_rendering.img_ready[1], nullptr);
}

void
update_game(gpu_t *gpu
	    , swapchain_t *swapchain
	    , window_data_t *window
	    , vulkan_state_t *vk
	    , float32_t dt)
{
    // ---- update different parts of the game (world, gui...)
    persist uint32_t current_frame = 0;
    persist constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;

    current_frame = 0;

    VkFence null_fence = VK_NULL_HANDLE;
    
    auto next_image_data = acquire_next_image(swapchain
                                              , gpu
                                              , &window_rendering.img_ready[current_frame]
                                              , &null_fence);
    
    if (next_image_data.result == VK_ERROR_OUT_OF_DATE_KHR)
    {
	// ---- recreate swapchain ----
	return;
    }
    else if (next_image_data.result != VK_SUCCESS && next_image_data.result != VK_SUBOPTIMAL_KHR)
    {
	OUTPUT_DEBUG_LOG("%s\n", "failed to acquire swapchain image");
    }
    
    wait_fences(gpu, memory_buffer_view_t<VkFence>{1, &window_rendering.cpu_wait[current_frame]});
    reset_fences(gpu, {1, &window_rendering.cpu_wait[current_frame]});
    
    // ---- begin recording instructions into the command buffers ----
    gpu_command_queue_t queue{window_rendering.command_buffer[current_frame]};
    begin_command_buffer(&queue.q, 0, nullptr);
    {
        update_world(window, vk, dt, next_image_data.image_index, current_frame, &queue);
        
        update_game_ui(&vk->gpu, get_pfx_framebuffer_hdl(), window);
        render_game_ui(&vk->gpu, get_pfx_framebuffer_hdl(), &queue);
        
        render_final_output(next_image_data.image_index, &queue, swapchain);
    }
    end_command_buffer(&queue.q);
    // ---- end recording instructions

    VkPipelineStageFlags wait_stages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;;

    submit(memory_buffer_view_t<VkCommandBuffer>{1, &window_rendering.command_buffer[current_frame]}
                               , memory_buffer_view_t<VkSemaphore>{1, &window_rendering.img_ready[current_frame]}
                               , memory_buffer_view_t<VkSemaphore>{1, &window_rendering.render_finish[current_frame]}
                               , memory_buffer_view_t<VkPipelineStageFlags>{1, &wait_stages}
                               , &window_rendering.cpu_wait[current_frame]
                               , &gpu->graphics_queue);
    
    VkSemaphore signal_semaphores[] = {window_rendering.render_finish[current_frame]};

    present(memory_buffer_view_t<VkSemaphore>{1, &window_rendering.render_finish[current_frame]}
                                , memory_buffer_view_t<VkSwapchainKHR>{1, &swapchain->swapchain}
                                , &next_image_data.image_index
                                , &gpu->present_queue);
    
    if (next_image_data.result == VK_ERROR_OUT_OF_DATE_KHR || next_image_data.result == VK_SUBOPTIMAL_KHR)
    {
	// recreate swapchain
    }
    else if (next_image_data.result != VK_SUCCESS)
    {
	OUTPUT_DEBUG_LOG("%s\n", "failed to present swapchain image");
    }    

    current_frame = (current_frame + 1) % MAX_FRAMES_IN_FLIGHT;
}
