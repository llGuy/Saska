#pragma once

#include "raw_input.hpp"
#include "vulkan.hpp"
#include "graphics.hpp"
#include "ui.hpp"
#include "script.hpp"
#include "net.hpp"
#include "event_system.hpp"

// This just decides whether the game should be run with a console, or with graphics. For servers, it would be better (if not debugging, to use console instead of having to initialize a vulkan context, graphics state, ui, etc...)
// The client will always use WINDOW_APPLICATION_MODE
enum application_type_t { WINDOW_APPLICATION_MODE, CONSOLE_APPLICATION_MODE };

enum element_focus_t { WORLD_3D_ELEMENT_FOCUS, UI_ELEMENT_CONSOLE, UI_ELEMENT_MENU, UI_ELEMENT_INPUT };

struct focus_stack_t {
    int32_t current_foci = -1;
    element_focus_t foci [ 10 ];

    void pop_focus(void) {
        --current_foci;
    }
    
    void push_focus(element_focus_t focus) {
        foci[++current_foci] = focus;
    }
    
    element_focus_t get_current_focus(void) {
        return foci[current_foci];
    }
};

struct game_memory_t {
    graphics_t graphics_state;
    graphics_context_t graphics_context;
    user_interface_t user_interface_state;


    // If in CONSOLE_MODE, don't render anything, don't create window, don't create graphics context
    application_type_t app_type;

    // Which screen has the focus (3D scene screen, ui console screen, ui menu screen, ...)
    focus_stack_t focus_stack;


    event_dispatcher_t event_dispatcher;
};

void load_game(game_memory_t *memory);
void initialize_game(game_memory_t *memory, raw_input_t *raw_input, create_vulkan_surface *create_surface_proc, application_mode_t app_mode, application_type_t app_type);
void destroy_game(game_memory_t *memory);

void game_tick(game_memory_t *memory, raw_input_t *raw_input, float32_t dt);

gpu_command_queue_pool_t *get_global_command_pool(void);
application_type_t get_app_type(void);
application_mode_t get_app_mode(void);

void clear_and_request_focus(element_focus_t focus);
void request_focus(element_focus_t focus);
