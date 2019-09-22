#include "world.hpp"
#include "vulkan.hpp"
#include "graphics.hpp"

#include "script.hpp"
#include "ui.hpp"
#include "network.hpp"

global_var game_memory_t *g_game_memory;

void load_game(game_memory_t *memory)
{
    g_game_memory = memory;

    initialize_network_translation_unit(memory);
    initialize_world_translation_unit(memory);
    initialize_vulkan_translation_unit(memory);
    initialize_graphics_translation_unit(memory);
    initialize_ui_translation_unit(memory);
    initialize_script_translation_unit(memory);
}

void initialize_game(game_memory_t *memory, input_state_t *input_state, create_vulkan_surface *create_surface_proc, network_state_t::application_mode_t app_mode, application_type_t app_type)
{
    memory->app_type = app_type;
    
    // ---- Initialize game data ----
    switch (memory->app_type)
    {
    case application_type_t::WINDOW_APPLICATION_MODE:
        {
            // Initialize graphics api, atmosphere, shadow, skeletal animation...
            initialize_scripting();
            initialize_network_state(memory, app_mode);
            graphics_api_initialize_ret_t graphics = initialize_graphics_api(create_surface_proc, input_state);
            initialize_game_3d_graphics(graphics.command_pool);
            initialize_game_2d_graphics(graphics.command_pool);
            initialize_game_ui(graphics.command_pool, g_uniform_pool, get_backbuffer_resolution());
            initialize_world(input_state, graphics.command_pool, app_type);
        } break;
    case application_type_t::CONSOLE_APPLICATION_MODE:
        {
            initialize_network_state(memory, app_mode);
            initialize_scripting();
            initialize_world(input_state, VK_NULL_HANDLE, app_type);
        } break;
    }
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
    switch (memory->app_type)
    {
    case application_type_t::WINDOW_APPLICATION_MODE:
        {
            // ---- begin recording instructions into the command buffers ----
            frame_rendering_data_t frame = begin_frame_rendering(input_state);
            gpu_command_queue_t queue{frame.command_buffer};
            {
                update_network_state();
                update_world(input_state, dt, frame.image_index, 0 /* Don't need this argument */, &queue, memory->app_type);

                dbg_handle_input(input_state);
                update_game_ui(get_pfx_framebuffer_hdl(), input_state);
                render_game_ui(get_pfx_framebuffer_hdl(), &queue);
        
                render_final_output(frame.image_index, &queue);
            }
            end_frame_rendering_and_refresh(input_state);
        } break;
    case application_type_t::CONSOLE_APPLICATION_MODE:
        {
            update_network_state();
            update_world(input_state, dt, 0, 0 /* Don't need this argument */, nullptr, memory->app_type);
        } break;
    }
}

application_type_t get_app_type(void)
{
    return g_game_memory->app_type;
}
