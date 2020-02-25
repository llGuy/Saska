#include "gamestate.hpp"
#include "game.hpp"
#include "vulkan.hpp"
#include "graphics.hpp"

#include "variables.hpp"
#include "script.hpp"
#include "ui.hpp"
#include "net.hpp"
#include "game_input.hpp"
#include "menu.hpp"
#include <ctime>

#include "chunks_gstate.hpp"

#include "deferred_renderer.hpp"

static game_memory_t *g_game_memory;
static event_dispatcher_t dispatcher;

void load_game(game_memory_t *memory) {
    srand((uint32_t)time((time_t)NULL));
    
    g_game_memory = memory;

    initialize_vulkan_translation_unit(memory);
    initialize_graphics_translation_unit(memory);
    initialize_ui_translation_unit(memory);
    initialize_script_translation_unit(memory);
}

void initialize_game(game_memory_t *memory, raw_input_t *raw_input, create_vulkan_surface *create_surface_proc, application_mode_t app_mode, application_type_t app_type) {
    memory->app_type = app_type;
    
    // ---- Initialize game data ----
    switch (memory->app_type) {
    case application_type_t::WINDOW_APPLICATION_MODE: {
        if (app_mode == application_mode_t::CLIENT_MODE) {
            load_variables();
        }

            
            
        // Initialize graphics api, atmosphere, shadow, skeletal animation...
        initialize_scripting();
        initialize_net(app_mode, &dispatcher);
        graphics_api_initialize_ret_t graphics = initialize_graphics_api(create_surface_proc, raw_input);
        initialize_game_3d_graphics(graphics.command_pool, raw_input);
        initialize_game_2d_graphics(graphics.command_pool);
        initialize_game_ui(graphics.command_pool, g_uniform_pool, backbuffer_resolution());

        if (app_mode == application_mode_t::CLIENT_MODE) {
            if (strlen(variables_get_user_name()) == 0) {
                // Need to prompt user for user name
                prompt_user_for_name();
            }
        }
            
        initialize_gamestate(raw_input, &dispatcher);
    } break;
    case application_type_t::CONSOLE_APPLICATION_MODE: {
        initialize_net(app_mode, &dispatcher);
        initialize_scripting();
        initialize_gamestate(raw_input, &dispatcher);
    } break;
    }

    initialize_game_input_settings();

    memory->focus_stack.push_focus(element_focus_t::UI_ELEMENT_MENU);
}

void destroy_game(game_memory_t *memory) {
    if (get_app_mode() == application_mode_t::CLIENT_MODE) {
        save_variables();
    }

    destroy_swapchain();
    
    g_render_pass_manager->clean_up();
    g_image_manager->clean_up();
    g_framebuffer_manager->clean_up();
    g_pipeline_manager->clean_up();
    g_gpu_buffer_manager->clean_up();

    deinitialize_chunks_state();

    deinitialize_net();
    
    destroy_graphics();

    destroy_vulkan_state();
}

// Decides which element gets input focus
static void s_handle_global_game_input(game_memory_t *memory, raw_input_t *raw_input) {
    if (raw_input->char_stack[0] == '/' && memory->focus_stack.get_current_focus() != element_focus_t::UI_ELEMENT_CONSOLE && memory->focus_stack.get_current_focus() != element_focus_t::UI_ELEMENT_INPUT) {
        memory->focus_stack.push_focus(element_focus_t::UI_ELEMENT_CONSOLE);
    }
    else if (raw_input->buttons[button_type_t::ESCAPE].state) {
        if (memory->focus_stack.get_current_focus() == element_focus_t::WORLD_3D_ELEMENT_FOCUS) {
            memory->focus_stack.push_focus(element_focus_t::UI_ELEMENT_MENU);
            enable_cursor_display();
        }
        else {
            // Main menu needs to be at the beginning of the stack always
            if (memory->focus_stack.current_foci > 0) {
                memory->focus_stack.pop_focus();

                if (memory->focus_stack.get_current_focus() == element_focus_t::WORLD_3D_ELEMENT_FOCUS) {
                    disable_cursor_display();
                }
            }
        }
    }
}

void game_tick(game_memory_t *memory, raw_input_t *raw_input, float32_t dt) {
    switch (memory->app_type) {
    case application_type_t::WINDOW_APPLICATION_MODE: {
        game_input_t game_input = {};
        translate_raw_to_game_input(raw_input, &game_input, dt, memory->focus_stack.get_current_focus());
            
            
            
        s_handle_global_game_input(memory, raw_input);
        tick_net(raw_input, dt);
        hotreload_assets_if_changed();

            
            
        // ---- begin recording instructions into the command buffers ----
        frame_rendering_data_t frame = begin_frame_rendering(raw_input);
        gpu_command_queue_t queue{frame.command_buffer};
        {
            // Important function: may need to change structure of world API to incorporate networking
            tick_gamestate(&game_input, dt, &queue, memory->app_type, memory->focus_stack.get_current_focus());

            dbg_handle_input(raw_input);
            update_game_ui(get_pfx_framebuffer_hdl(), raw_input, memory->focus_stack.get_current_focus(), &dispatcher);
            render_game_ui(get_pfx_framebuffer_hdl(), &queue);
        
            render_final_output(frame.image_index, &queue);
        }
        end_frame_rendering_and_refresh(raw_input);
    } break;
    case application_type_t::CONSOLE_APPLICATION_MODE: {
        tick_net(nullptr, dt);
        tick_gamestate(nullptr, dt, nullptr, memory->app_type, (element_focus_t)0);
    } break;
    }

    dispatcher.dispatch_events();
}

application_type_t get_app_type(void) {
    return g_game_memory->app_type;
}


void clear_and_request_focus(element_focus_t focus) {
    g_game_memory->focus_stack.current_foci = 1;
    g_game_memory->focus_stack.foci[1] = focus;

    if (focus == WORLD_3D_ELEMENT_FOCUS) {
        disable_cursor_display();
    }
    else {
        enable_cursor_display();
    }
}

void request_focus(element_focus_t focus) {
    g_game_memory->focus_stack.push_focus(focus);
    
    if (focus == WORLD_3D_ELEMENT_FOCUS) {
        disable_cursor_display();
    }
    else {
        enable_cursor_display();
    }
}

