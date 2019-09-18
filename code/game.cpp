#include "world.hpp"
#include "vulkan.hpp"
#include "graphics.hpp"

#include "script.hpp"
#include "ui.hpp"
#include "network.hpp"

void load_game(game_memory_t *memory)
{
    initialize_network_translation_unit(memory);
    initialize_world_translation_unit(memory);
    initialize_vulkan_translation_unit(memory);
    initialize_graphics_translation_unit(memory);
    initialize_ui_translation_unit(memory);
    initialize_script_translation_unit(memory);
}

void initialize_game(game_memory_t *memory, input_state_t *input_state, create_vulkan_surface *create_surface_proc, network_state_t::application_mode_t app_mode)
{
    // ---- Initialize game data ----
    // Initialize graphics api, atmosphere, shadow, skeletal animation...
    initialize_network_state(memory, app_mode);
    graphics_api_initialize_ret_t graphics = initialize_graphics_api(create_surface_proc, input_state);
    initialize_scripting();
    initialize_game_3d_graphics(graphics.command_pool);
    initialize_game_2d_graphics(graphics.command_pool);
    initialize_game_ui(graphics.command_pool, g_uniform_pool, get_backbuffer_resolution());
    initialize_world(input_state, graphics.command_pool);
}

void destroy_game(game_memory_t *memory)
{
    destroy_swapchain();
    
    // ---- destroy world data ----
    destroy_world();

    destroy_vulkan_state();
}

void game_tick(game_memory_t *memory, input_state_t *input_state, float32_t dt)
{
    // ---- begin recording instructions into the command buffers ----
    frame_rendering_data_t frame = begin_frame_rendering();
    gpu_command_queue_t queue{frame.command_buffer};
    {
        update_network_state();
        update_world(input_state, dt, frame.image_index, 0 /* Don't need this argument */, &queue);

        dbg_handle_input(input_state);
        update_game_ui(get_pfx_framebuffer_hdl(), input_state);
        render_game_ui(get_pfx_framebuffer_hdl(), &queue);
        
        render_final_output(frame.image_index, &queue);
    }
    end_frame_rendering_and_refresh();
}
