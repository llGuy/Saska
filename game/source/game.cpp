#include "world.hpp"
#include "vulkan.hpp"
#include "graphics.hpp"

#include "script.hpp"
#include "ui.hpp"
#include "network.hpp"
#include <ctime>

global_var game_memory_t *g_game_memory;

void load_game(game_memory_t *memory)
{
    srand(time(NULL));
    
    g_game_memory = memory;

    initialize_network_translation_unit(memory);
    initialize_world_translation_unit(memory);
    initialize_vulkan_translation_unit(memory);
    initialize_graphics_translation_unit(memory);
    initialize_ui_translation_unit(memory);
    initialize_script_translation_unit(memory);
}

void initialize_game(game_memory_t *memory, input_state_t *input_state, create_vulkan_surface *create_surface_proc, application_mode_t app_mode, application_type_t app_type)
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
            initialize_game_3d_graphics(graphics.command_pool, input_state);
            initialize_game_2d_graphics(graphics.command_pool);
            initialize_game_ui(graphics.command_pool, g_uniform_pool, get_backbuffer_resolution());
            hard_initialize_world(input_state, graphics.command_pool, app_type, app_mode);
        } break;
    case application_type_t::CONSOLE_APPLICATION_MODE:
        {
            initialize_network_state(memory, app_mode);
            initialize_scripting();
            initialize_world(input_state, VK_NULL_HANDLE, app_type, app_mode);
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

// Decides which element gets input focus
internal_function void handle_global_game_input(game_memory_t *memory, input_state_t *input_state)
{
    if (input_state->char_stack[0] == 't' && memory->screen_focus != element_focus_t::UI_ELEMENT_CONSOLE)
    {
        input_state->char_stack[0] = 0;
        
        memory->screen_focus = element_focus_t::UI_ELEMENT_CONSOLE;
    }
    else if (input_state->keyboard[keyboard_button_type_t::ESCAPE].is_down)
    {
        switch (memory->screen_focus)
        {
        case element_focus_t::UI_ELEMENT_CONSOLE:
            {
                memory->screen_focus = element_focus_t::WORLD_3D_ELEMENT_FOCUS;
            } break;
        case element_focus_t::WORLD_3D_ELEMENT_FOCUS:
            {
                // Set focus to like some escape menu or something
            } break;
        }
    }
}

void game_tick(game_memory_t *memory, input_state_t *input_state, float32_t dt)
{
    switch (memory->app_type)
    {
    case application_type_t::WINDOW_APPLICATION_MODE:
        {
            handle_global_game_input(memory, input_state);
            update_network_state();
            hotreload_assets_if_changed();
            
            // ---- begin recording instructions into the command buffers ----
            frame_rendering_data_t frame = begin_frame_rendering(input_state);
            gpu_command_queue_t queue{frame.command_buffer};
            {
                update_world(input_state, dt, frame.image_index, 0 /* Don't need this argument */, &queue, memory->app_type, memory->screen_focus);

                dbg_handle_input(input_state);
                update_game_ui(get_pfx_framebuffer_hdl(), input_state, memory->screen_focus);
                render_game_ui(get_pfx_framebuffer_hdl(), &queue);
        
                render_final_output(frame.image_index, &queue);
            }
            end_frame_rendering_and_refresh(input_state);
        } break;
    case application_type_t::CONSOLE_APPLICATION_MODE:
        {
            update_network_state();
            update_world(input_state, dt, 0, 0 /* Don't need this argument */, nullptr, memory->app_type, memory->screen_focus);
        } break;
    }
}

application_type_t get_app_type(void)
{
    return g_game_memory->app_type;
}
