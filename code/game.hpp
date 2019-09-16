#pragma once

#include "vulkan.hpp"
#include "world.hpp"
#include "graphics.hpp"
#include "ui.hpp"
#include "script.hpp"

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
};

void load_game(game_memory_t *memory);

void initialize_game(game_memory_t *memory, input_state_t *input_state, create_vulkan_surface *create_surface_proc);

void destroy_game(game_memory_t *memory);

void game_tick(game_memory_t *memory, input_state_t *input_state, float32_t dt);

gpu_command_queue_pool_t *get_global_command_pool(void);
