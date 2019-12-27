#include "world.hpp"
#include "vulkan.hpp"
#include "graphics.hpp"

#include "script.hpp"
#include "ui.hpp"
#include "network.hpp"
#include <ctime>
#include "sk_game.hpp"



// Global
static application_type_t application_type;
static element_focus_t current_element_focus;



void initialize_sk_game(display_window_information_t window_info, application_type_t type)
{
    application_type = type;

    
    initialize_scripting();
    
    
    initialize_network_state(app_mode);
    graphics_api_initialize_ret_t graphics = initialize_graphics_api(window_info);
    initialize_game_3d_graphics(graphics.command_pool, input_state);
    initialize_game_2d_graphics(graphics.command_pool);
    initialize_game_ui(graphics.command_pool, g_uniform_pool, get_backbuffer_resolution());
    hard_initialize_world(input_state, graphics.command_pool, app_type, app_mode);
}


void destroy_sk_game(void)
{
}


// Decides which element gets input focus
static void handle_global_game_input(game_memory_t *memory, input_state_t *input_state)
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


void tick(raw_input_t *raw_input, float32_t dt)
{
    handle_global_game_input(memory, input_state);
    update_network_state(input_state, dt);
    hotreload_assets_if_changed();
            
    // ---- begin recording instructions into the command buffers ----
    frame_rendering_data_t frame = begin_frame_rendering(input_state);
    gpu_command_queue_t queue{frame.command_buffer};
    {
        // Important function: may need to change structure of world API to incorporate networking
        tick_world(input_state, dt, frame.image_index, 0 /* Don't need this argument */, &queue, memory->app_type, memory->screen_focus);

        dbg_handle_input(input_state);
        update_game_ui(get_pfx_framebuffer_hdl(), input_state, memory->screen_focus);
        render_game_ui(get_pfx_framebuffer_hdl(), &queue);
        
        render_final_output(frame.image_index, &queue);
    }
    end_frame_rendering_and_refresh(input_state);
}
