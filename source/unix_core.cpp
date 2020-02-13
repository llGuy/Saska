/*#if defined (UNIX_BUILD)

// For the moment, unix build is just for server stuff

#include "game.hpp"
#include "utility.hpp"
#include "memory.hpp"
#include "allocators.hpp"

#include <time.h>

static bool running;
static double dt = 0.0f;
static game_memory_t game;

int32_t main(int32_t argc, char *argv[])
{
    output_to_debug_console("Starting session ----\n");

    // Initialize game's dynamic memory
    linear_allocator_global.capacity = (uint32_t)megabytes(30);
    linear_allocator_global.start = linear_allocator_global.current = malloc(linear_allocator_global.capacity);

    stack_allocator_global.capacity = (uint32_t)megabytes(10);
    stack_allocator_global.start = stack_allocator_global.current = malloc(stack_allocator_global.capacity);

    free_list_allocator_global.available_bytes = (uint32_t)megabytes(30);
    free_list_allocator_global.start = malloc(free_list_allocator_global.available_bytes);

    free_list_allocator_global.free_block_head = (free_block_header_t *)free_list_allocator_global.start;
    free_list_allocator_global.free_block_head->free_block_size = free_list_allocator_global.available_bytes;

    application_type_t app_type = application_type_t::CONSOLE_APPLICATION_MODE;
    application_mode_t app_mode = application_mode_t::SERVER_MODE;

    load_game(&game);

    initialize_game(&game, nullptr, nullptr, app_mode, app_type);

    while (running)
    {
        clock_t start = clock();

        game_tick(&game, nullptr, dt);

        clock_t end = clock();

        dt = (float32_t)(end - start) / CLOCKS_PER_SEC;
    }

    return(0);
}

void request_quit(void)
{
    running = 0;
}

raw_input_t *get_raw_input(void)
{
    return nullptr;
}

void output_to_debug_console_i(int32_t i)
{
    char buffer[15] = {};
    sprintf_s(buffer, "%i\0", i);
    printf(buffer);
}


void output_to_debug_console_i(float32_t f)
{
    char buffer[15] = {};
    sprintf_s(buffer, "%f\0", f);
    printf(buffer);
}


void output_to_debug_console_i(const vector3_t &v3)
{
    output_to_debug_console((float32_t)(v3[0]), "|", (float32_t)(v3[1]), "|", (float32_t)(v3[2]));
}


void output_to_debug_console_i(const quaternion_t &q4)
{
    output_to_debug_console((float32_t)(q4[0]), "|", (float32_t)(q4[1]), "|", (float32_t)(q4[2]), "|", (float32_t)(q4[3]));
}


void output_to_debug_console_i(const char *string)
{
    printf(string);
}

void send_vibration_to_gamepad(void)
{
}

void enable_cursor_display(void)
{
}


void disable_cursor_display(void)
{
}


#endif*/