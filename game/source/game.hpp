#pragma once

#include "vulkan.hpp"
#include "world.hpp"
#include "graphics.hpp"
#include "ui.hpp"
#include "script.hpp"
#include "network.hpp"

// This just decides whether the game should be run with a console, or with graphics. For servers, it would be better (if not debugging, to use console instead of having to initialize a vulkan context, graphics state, ui, etc...)
// The client will always use WINDOW_APPLICATION_MODE
enum application_type_t { WINDOW_APPLICATION_MODE, CONSOLE_APPLICATION_MODE };

enum element_focus_t { WORLD_3D_ELEMENT_FOCUS, UI_ELEMENT_CONSOLE, UI_ELEMENT_MENU };

struct game_memory_t
{
    // Contains the entities, and the terrains, and possibly more
    struct world_t world_state;
    // Contains all state to do with graphics: material queues, GPU object managers, etc...
    struct graphics_t graphics_state;
    struct graphics_context_t graphics_context;
    // User interface stuff
    struct user_interface_t user_interface_state;
    // Script stuff
    struct game_scripts_t script_state;
    // Network stuff: client name, server address, etc...
    struct network_state_t network_state;

    // If in CONSOLE_MODE, don't render anything, don't create window, don't create graphics context
    application_type_t app_type;

    // Which screen has the focus (3D scene screen, ui console screen, ui menu screen, ...)
    element_focus_t screen_focus = WORLD_3D_ELEMENT_FOCUS;
};

void load_game(game_memory_t *memory);
void initialize_game(game_memory_t *memory, input_state_t *input_state, create_vulkan_surface *create_surface_proc, application_mode_t app_mode, application_type_t app_type);
void destroy_game(game_memory_t *memory);

void game_tick(game_memory_t *memory, input_state_t *input_state, float32_t dt);

gpu_command_queue_pool_t *get_global_command_pool(void);
application_type_t get_app_type(void);
application_mode_t get_app_mode(void);
