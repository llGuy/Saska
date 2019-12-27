#pragma once

#include "utils.hpp"

// Platform specific stuff
void show_cursor(void);
void hide_cursor(void);

struct display_window_information_t
{
    void *window;
    uint32_t width, height;
    // May need more information
};

// Console stuff
void output_to_debug_console_i(float32_t f);
void output_to_debug_console_i(const vector3_t &v3);
void output_to_debug_console_i(const quaternion_t &q4);
void output_to_debug_console_i(int32_t i);
void output_to_debug_console_i(const char *string);

template <typename ...T> void output_to_debug_console(T &&...ts)
{
    // Could do this with C++17
    //(output_to_debug_console_i(ts), ...);

    // But in case C++17 is not supported:
    char dummy[] = { 0, (output_to_debug_console_i(ts), 0)... };
}
