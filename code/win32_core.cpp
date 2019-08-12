#define NOMINMAX

#include <stb_image.h>

#define VK_USE_PLATFORM_WIN32_KHR

#include "core.hpp"
#include "utils.hpp"

#if defined(UNITY_BUILD)
#include <vulkan/vulkan.h>
#include "memory.cpp"
#include "ui.cpp"
#include "game.cpp"
#include "world.cpp"
#include "script.cpp"
#include "vulkan.cpp"
#include "graphics.cpp"
#else

#include "memory.hpp"

#include "vulkan.hpp"
#include "core.hpp"
#include "world.hpp"
#include <stdlib.h>

#include "game.hpp"
#include "vulkan.hpp"

#include <windows.h>
#endif

#include <windows.h>

#define DEBUG_FILE ".debug"

debug_output_t output_file;

global_var bool g_running;
global_var double g_game_time = 0.0f;
global_var double g_dt = 0.0f;
global_var HWND g_window;
global_var HCURSOR g_cursor;
global_var input_state_t input_state = {};

file_contents_t
read_file(const char *filename, const char *flags, linear_allocator_t *allocator)
{
    FILE *file = fopen(filename, flags);
    if (file == nullptr)
    {
	OUTPUT_DEBUG_LOG("error - couldnt load file \"%s\"\n", filename);
	assert(false);
    }
    fseek(file, 0, SEEK_END);
    uint32_t size = ftell(file);
    rewind(file);

    byte_t *buffer = (byte_t *)allocate_linear(size + 1);
    
    fread(buffer, 1, size, file);

    buffer[size] = '\0';
    
    fclose(file);

    file_contents_t contents { size, buffer };
    
    return(contents);
}

external_image_data_t
read_image(const char *filename)
{
    external_image_data_t external_image_data;
    external_image_data.pixels = stbi_load(filename,
                                           &external_image_data.width,
                                           &external_image_data.height,
                                           &external_image_data.channels,
                                           STBI_rgb_alpha);
    return(external_image_data);
}

internal_function void
open_debug_file(void)
{
    output_file.fp = fopen(DEBUG_FILE, "w+");
    assert(output_file.fp >= NULL);
}

internal_function void
close_debug_file(void)
{
    fclose(output_file.fp);
}

extern void
output_debug(const char *format, ...)
{
    va_list arg_list;
    
    va_start(arg_list, format);

    fprintf(output_file.fp
	    , format
	    , arg_list);

    va_end(arg_list);

    fflush(output_file.fp);
}

float32_t
barry_centric(const vector3_t &p1, const vector3_t &p2, const vector3_t &p3, const vector2_t &pos)
{
    float32_t det = (p2.z - p3.z) * (p1.x - p3.x) + (p3.x - p2.x) * (p1.z - p3.z);
    float32_t l1 = ((p2.z - p3.z) * (pos.x - p3.x) + (p3.x - p2.x) * (pos.y - p3.z)) / det;
    float32_t l2 = ((p3.z - p1.z) * (pos.x - p3.x) + (p1.x - p3.x) * (pos.y - p3.z)) / det;
    float32_t l3 = 1.0f - l1 - l2;
    return l1 * p1.y + l2 * p2.y + l3 * p3.y;
}

void enable_cursor_display(void)
{
    SetCursor(g_cursor);
}

void disable_cursor_display(void)
{    
    SetCursor(0);
}

LRESULT CALLBACK windows_callback(HWND window_handle,
                                  UINT message,
                                  WPARAM wparam,
                                  LPARAM lparam)
{
    switch(message)
    {
        // Input ...


    case WM_SETCURSOR:
        {
            SetCursor(0);
        } break;
    case WM_DESTROY:
        {
            g_running = 0;
            PostQuitMessage(0);
        } break;
    case WM_PAINT:
        {
            // Render
            update_game(&input_state, g_dt);
        } break;
    default:
        {
            return(DefWindowProc(window_handle, message, wparam, lparam));
        } break;
    }

    return(0);
}

struct create_vulkan_surface_win32 : create_vulkan_surface
{
    HWND *window_ptr;
    bool32_t create_proc(void) override
    {
        VkWin32SurfaceCreateInfoKHR create_info = {};
        create_info.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
        create_info.hwnd = *window_ptr;
        create_info.hinstance = GetModuleHandle(nullptr);
        return vkCreateWin32SurfaceKHR(*instance, &create_info, nullptr, surface);
    }
};

internal_function void
init_free_list_allocator_head(free_list_allocator_t *allocator = &free_list_allocator_global)
{
    allocator->free_block_head = (free_block_header_t *)allocator->start;
    allocator->free_block_head->free_block_size = allocator->available_bytes;
}

#include <chrono>

int32_t CALLBACK WinMain(HINSTANCE hinstance,
                         HINSTANCE prev_instance,
                         LPSTR cmdline,
                         int32_t showcmd)
{
    // Initialize game's dynamic memory
    linear_allocator_global.capacity = megabytes(30);
    linear_allocator_global.start = linear_allocator_global.current = malloc(linear_allocator_global.capacity);
	
    stack_allocator_global.capacity = megabytes(10);
    stack_allocator_global.start = stack_allocator_global.current = malloc(stack_allocator_global.capacity);

    free_list_allocator_global.available_bytes = megabytes(10);
    free_list_allocator_global.start = malloc(free_list_allocator_global.available_bytes);
    init_free_list_allocator_head(&free_list_allocator_global);
    
    const char *window_class_name = "saska_window_class";
    const char *window_name = "Saska";

    g_cursor = LoadCursor(0, IDC_ARROW);
    
    WNDCLASS window_class = {};
    ZeroMemory(&window_class, sizeof(window_class));
    window_class.style = CS_OWNDC | CS_VREDRAW | CS_HREDRAW;
    window_class.lpfnWndProc = windows_callback;
    window_class.cbClsExtra = 0;
    window_class.hInstance = hinstance;
    window_class.hIcon = 0;
    window_class.hCursor = g_cursor;
    window_class.hbrBackground = 0;
    window_class.lpszMenuName = 0;
    window_class.lpszClassName = window_class_name;

    assert(RegisterClass(&window_class));

    g_window = CreateWindowEx(0,
                              window_class_name,
                              window_name,
                              WS_OVERLAPPEDWINDOW,
                              CW_USEDEFAULT,
                              CW_USEDEFAULT,
                              CW_USEDEFAULT,
                              CW_USEDEFAULT,
                              NULL,
                              NULL,
                              hinstance,
                              NULL);

    ShowWindow(g_window, showcmd);
    //    disable_cursor_display();

    create_vulkan_surface_win32 create_surface_proc_win32 = {};
    create_surface_proc_win32.window_ptr = &g_window;
    
    initialize_graphics_api(&create_surface_proc_win32, &input_state);
    initialize_game(&input_state);

    g_running = 1;

    auto now = std::chrono::high_resolution_clock::now();
    while(g_running)
    {
        MSG message;
        while(GetMessage(&message, NULL, 0, 0))
        {
            TranslateMessage(&message);
            DispatchMessage(&message);
        }
        
        RedrawWindow(g_window, NULL, NULL, RDW_INTERNALPAINT);
        
        clear_linear();

        input_state.cursor_moved = false;

        input_state.char_count = 0;
        input_state.keyboard[keyboard_button_type_t::BACKSPACE].is_down = false;
        input_state.keyboard[keyboard_button_type_t::ENTER].is_down = false;
        input_state.keyboard[keyboard_button_type_t::ESCAPE].is_down = false;
        input_state.keyboard[keyboard_button_type_t::LEFT_CONTROL].is_down = false;

        // TODO: Set normalized cursor position
        //        input_state.normalized_cursor_position = vector2_t((float32_t)xpos, (float32_t)ypos);
        
        auto new_now = std::chrono::high_resolution_clock::now();
        g_dt = std::chrono::duration<double, std::chrono::seconds::period>(new_now - now).count();
        g_game_time += g_dt;
        input_state.dt = g_dt;

        now = new_now;
    }

    close_debug_file();

    destroy_swapchain();
    destroy_game();
	
    destroy_vulkan_state();

    return(0);
}
