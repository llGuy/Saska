/* win32_core.cpp */

#define NOMINMAX

#include <stb_image.h>

#define VK_USE_PLATFORM_WIN32_KHR


/*#include "core.hpp"
#include "utils.hpp"

#include "game.hpp"*/

#if defined(UNITY_BUILD)
#include "network.cpp"
#include <vulkan/vulkan.h>
#include "memory.cpp"
#include "ui.cpp"
#include "game.cpp"
#include "world.cpp"
#include "script.cpp"
#include "vulkan.cpp"
#include "graphics.cpp"
#include "file_system.cpp"
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

#include <Windows.h>
#include <Windowsx.h>

#define DEBUG_FILE ".debug"

debug_output_t output_file;

global_var bool g_running;
global_var bool g_hasfocus;
global_var double g_game_time = 0.0f;
global_var double g_dt = 0.0f;
global_var HWND g_window;
global_var HCURSOR g_cursor;
global_var input_state_t g_input_state = {};

input_state_t *get_input_state(void)
{
    return &g_input_state;
}

void request_quit(void)
{
    g_running = 0;
    PostQuitMessage(0);
}

/*file_contents_t read_file(const char *filename, const char *flags, linear_allocator_t *allocator)
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

external_image_data_t read_image(const char *filename)
{
    external_image_data_t external_image_data;
    external_image_data.pixels = stbi_load(filename,
                                           &external_image_data.width,
                                           &external_image_data.height,
                                           &external_image_data.channels,
                                           STBI_rgb_alpha);
    return(external_image_data);
    }*/

internal_function void open_debug_file(void)
{
    output_file.fp = fopen(DEBUG_FILE, "w+");
    assert(output_file.fp >= NULL);
}

internal_function void close_debug_file(void)
{
    fclose(output_file.fp);
}

void ouptut_debug_string(const char *string)
{
    OutputDebugString(string);
}

float32_t barry_centric(const vector3_t &p1, const vector3_t &p2, const vector3_t &p3, const vector2_t &pos)
{
    float32_t det = (p2.z - p3.z) * (p1.x - p3.x) + (p3.x - p2.x) * (p1.z - p3.z);
    float32_t l1 = ((p2.z - p3.z) * (pos.x - p3.x) + (p3.x - p2.x) * (pos.y - p3.z)) / det;
    float32_t l2 = ((p3.z - p1.z) * (pos.x - p3.x) + (p1.x - p3.x) * (pos.y - p3.z)) / det;
    float32_t l3 = 1.0f - l1 - l2;
    return l1 * p1.y + l2 * p2.y + l3 * p3.y;
}

void enable_cursor_display(void)
{
    ShowCursor(1);
}

void disable_cursor_display(void)
{    
    ShowCursor(0);
}

internal_function void handle_mouse_move_event(LPARAM lparam)
{
    persist_var int32_t true_prev_x = 0;
    persist_var int32_t true_prev_y = 0;

    RECT client_rect = {};
    GetClientRect(g_window, &client_rect);

    POINT cursor_pos = {};
    GetCursorPos(&cursor_pos);
    ScreenToClient(g_window, &cursor_pos);
    
    // In client space
    int32_t true_x_position = cursor_pos.x;
    int32_t true_y_position = cursor_pos.y;
    
    if (true_x_position != true_prev_x || true_y_position != true_prev_y)
    {
        g_input_state.cursor_moved = true;
        g_input_state.previous_cursor_pos_x = g_input_state.cursor_pos_x;
        g_input_state.previous_cursor_pos_y = g_input_state.cursor_pos_y;
    }
    
    int32_t true_diff_x = true_x_position - true_prev_x;
    int32_t true_diff_y = true_y_position - true_prev_y;

    // Virtual "infinite" cursor space
    g_input_state.cursor_pos_x += (float32_t)true_diff_x;
    g_input_state.cursor_pos_y += (float32_t)true_diff_y;
            
    true_prev_x = true_x_position;
    true_prev_y = true_y_position;
    
    if (true_x_position >= client_rect.right - 2)
    {
        POINT infinite = { 2, true_y_position };
        true_x_position = true_prev_x = 2;
        ClientToScreen(g_window, &infinite);
        SetCursorPos(infinite.x, infinite.y);
    }
    if (true_x_position <= client_rect.left + 1)
    {
        POINT infinite = { client_rect.right - 2, true_y_position };
        true_x_position = true_prev_x = client_rect.right - 2;
        ClientToScreen(g_window, &infinite);
        SetCursorPos(infinite.x, infinite.y);
    }
    if (true_y_position >= client_rect.bottom - 2)
    {
        POINT infinite = { true_x_position, client_rect.top + 2 };
        true_y_position = true_prev_y = client_rect.top + 2;
        ClientToScreen(g_window, &infinite);
        SetCursorPos(infinite.x, infinite.y);
    }
    if (true_y_position <= client_rect.top + 1)
    {
        POINT infinite = { true_x_position, client_rect.bottom - 2 };
        true_y_position = true_prev_y = client_rect.bottom - 2;
        ClientToScreen(g_window, &infinite);
        SetCursorPos(infinite.x, infinite.y);
    }

    POINT top_left = { client_rect.left, client_rect.top }, bottom_right = { client_rect.right, client_rect.bottom };
    ClientToScreen(g_window, &top_left);
    ClientToScreen(g_window, &bottom_right);

    client_rect.left = top_left.x;
    client_rect.top = top_left.y;
    client_rect.right = bottom_right.x;
    client_rect.bottom = bottom_right.y;

    ClipCursor(&client_rect);
}

enum key_action_t { KEY_ACTION_DOWN, KEY_ACTION_UP };

internal_function void set_key_state(input_state_t *input_state, keyboard_button_type_t button, int32_t action)
{
    if (action == key_action_t::KEY_ACTION_DOWN)
    {
        keyboard_button_input_t *key = &input_state->keyboard[button];
        switch(key->is_down)
        {
        case is_down_t::NOT_DOWN: {key->is_down = is_down_t::INSTANT;} break;
        case is_down_t::INSTANT: {key->is_down = is_down_t::REPEAT;} break;
        }
        key->down_amount += g_dt;
    }
    else if (action == key_action_t::KEY_ACTION_UP)
    {
        keyboard_button_input_t *key = &input_state->keyboard[button];
        key->is_down = is_down_t::NOT_DOWN;
        key->down_amount = 0.0f;
    }
}

internal_function void set_mouse_button_state(input_state_t *input_state, mouse_button_type_t button, int32_t action)
{
    if (action == key_action_t::KEY_ACTION_DOWN)
    {
        mouse_button_input_t *mouse_button = &input_state->mouse_buttons[button];
        switch(mouse_button->is_down)
        {
        case is_down_t::NOT_DOWN: {mouse_button->is_down = is_down_t::INSTANT;} break;
        case is_down_t::INSTANT: case is_down_t::REPEAT: {mouse_button->is_down = is_down_t::REPEAT;} break;
        }
        mouse_button->down_amount += g_dt;
    }
    else if (action == key_action_t::KEY_ACTION_UP)
    {
        mouse_button_input_t *mouse_button = &input_state->mouse_buttons[button];
        mouse_button->is_down = is_down_t::NOT_DOWN;
        mouse_button->down_amount = 0.0f;
    }
}

internal_function void handle_keyboard_event(WPARAM wparam, LPARAM lparam, int32_t action)
{
    switch(wparam)
    {
    case 0x41: { set_key_state(&g_input_state, keyboard_button_type_t::A, action); } break;
    case 0x42: { set_key_state(&g_input_state, keyboard_button_type_t::B, action); } break;
    case 0x43: { set_key_state(&g_input_state, keyboard_button_type_t::C, action); } break;
    case 0x44: { set_key_state(&g_input_state, keyboard_button_type_t::D, action); } break;
    case 0x45: { set_key_state(&g_input_state, keyboard_button_type_t::E, action); } break;
    case 0x46: { set_key_state(&g_input_state, keyboard_button_type_t::F, action); } break;
    case 0x47: { set_key_state(&g_input_state, keyboard_button_type_t::G, action); } break;
    case 0x48: { set_key_state(&g_input_state, keyboard_button_type_t::H, action); } break;
    case 0x49: { set_key_state(&g_input_state, keyboard_button_type_t::I, action); } break;
    case 0x4A: { set_key_state(&g_input_state, keyboard_button_type_t::J, action); } break;
    case 0x4B: { set_key_state(&g_input_state, keyboard_button_type_t::K, action); } break;
    case 0x4C: { set_key_state(&g_input_state, keyboard_button_type_t::L, action); } break;
    case 0x4D: { set_key_state(&g_input_state, keyboard_button_type_t::M, action); } break;
    case 0x4E: { set_key_state(&g_input_state, keyboard_button_type_t::N, action); } break;
    case 0x4F: { set_key_state(&g_input_state, keyboard_button_type_t::O, action); } break;
    case 0x50: { set_key_state(&g_input_state, keyboard_button_type_t::P, action); } break;
    case 0x51: { set_key_state(&g_input_state, keyboard_button_type_t::Q, action); } break;
    case 0x52: { set_key_state(&g_input_state, keyboard_button_type_t::R, action); } break;
    case 0x53: { set_key_state(&g_input_state, keyboard_button_type_t::S, action); } break;
    case 0x54: { set_key_state(&g_input_state, keyboard_button_type_t::T, action); } break;
    case 0x55: { set_key_state(&g_input_state, keyboard_button_type_t::U, action); } break;
    case 0x56: { set_key_state(&g_input_state, keyboard_button_type_t::V, action); } break;
    case 0x57: { set_key_state(&g_input_state, keyboard_button_type_t::W, action); } break;
    case 0x58: { set_key_state(&g_input_state, keyboard_button_type_t::X, action); } break;
    case 0x59: { set_key_state(&g_input_state, keyboard_button_type_t::Y, action); } break;
    case 0x5A: { set_key_state(&g_input_state, keyboard_button_type_t::Z, action); } break;
    case 0x30: { set_key_state(&g_input_state, keyboard_button_type_t::ZERO, action); } break;
    case 0x31: { set_key_state(&g_input_state, keyboard_button_type_t::ONE, action); } break;
    case 0x32: { set_key_state(&g_input_state, keyboard_button_type_t::TWO, action); } break;
    case 0x33: { set_key_state(&g_input_state, keyboard_button_type_t::THREE, action); } break;
    case 0x34: { set_key_state(&g_input_state, keyboard_button_type_t::FOUR, action); } break;
    case 0x35: { set_key_state(&g_input_state, keyboard_button_type_t::FIVE, action); } break;
    case 0x36: { set_key_state(&g_input_state, keyboard_button_type_t::SIX, action); } break;
    case 0x37: { set_key_state(&g_input_state, keyboard_button_type_t::SEVEN, action); } break;
    case 0x38: { set_key_state(&g_input_state, keyboard_button_type_t::EIGHT, action); } break;
    case 0x39: { set_key_state(&g_input_state, keyboard_button_type_t::NINE, action); } break;
    case VK_UP: { set_key_state(&g_input_state, keyboard_button_type_t::UP, action); } break;
    case VK_LEFT: { set_key_state(&g_input_state, keyboard_button_type_t::LEFT, action); } break;
    case VK_DOWN: { set_key_state(&g_input_state, keyboard_button_type_t::DOWN, action); } break;
    case VK_RIGHT: { set_key_state(&g_input_state, keyboard_button_type_t::RIGHT, action); } break;
    case VK_SPACE: { set_key_state(&g_input_state, keyboard_button_type_t::SPACE, action); } break;
    case VK_SHIFT: { set_key_state(&g_input_state, keyboard_button_type_t::LEFT_SHIFT, action); } break;
    case VK_CONTROL: { set_key_state(&g_input_state, keyboard_button_type_t::LEFT_CONTROL, action); } break;
    case VK_RETURN: { set_key_state(&g_input_state, keyboard_button_type_t::ENTER, action); } break;
    case VK_BACK: { set_key_state(&g_input_state, keyboard_button_type_t::BACKSPACE, action); } break;
    case VK_ESCAPE: { set_key_state(&g_input_state, keyboard_button_type_t::ESCAPE, action); } break;
    }
}

LRESULT CALLBACK windows_callback(HWND window_handle, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch(message)
    {
        // Input ...
    case WM_SIZE:
        {
            g_input_state.resized = 1;
            g_input_state.window_width = LOWORD(lparam);
            g_input_state.window_height = HIWORD(lparam);
        } break;
    case WM_MOUSEMOVE: { if (g_hasfocus) handle_mouse_move_event(lparam); } break;
    case WM_KEYDOWN: { if (g_hasfocus) handle_keyboard_event(wparam, lparam, key_action_t::KEY_ACTION_DOWN); } break;
    case WM_CHAR:
        {
            if (g_input_state.char_count != MAX_CHARS && g_hasfocus)
            {
                if (wparam >= 32)
                {
                    g_input_state.char_stack[g_input_state.char_count++] = (char)wparam;
                }
            }
        } break;
    case WM_KEYUP: { if (g_hasfocus) handle_keyboard_event(wparam, lparam, key_action_t::KEY_ACTION_UP); } break;
    case WM_RBUTTONDOWN: { if (g_hasfocus) set_mouse_button_state(&g_input_state, mouse_button_type_t::MOUSE_RIGHT, key_action_t::KEY_ACTION_DOWN); } break;
    case WM_RBUTTONUP: { if (g_hasfocus) set_mouse_button_state(&g_input_state, mouse_button_type_t::MOUSE_RIGHT, key_action_t::KEY_ACTION_UP); } break;
    case WM_LBUTTONDOWN: { if (g_hasfocus) set_mouse_button_state(&g_input_state, mouse_button_type_t::MOUSE_LEFT, key_action_t::KEY_ACTION_DOWN); } break;
    case WM_LBUTTONUP: { if (g_hasfocus) set_mouse_button_state(&g_input_state, mouse_button_type_t::MOUSE_LEFT, key_action_t::KEY_ACTION_UP); } break;
        // NOTE: Make sure this doesn't break anything in the future
    case WM_SETCURSOR: { SetCursor(0);} break;
    case WM_CLOSE: { g_running = 0; PostQuitMessage(0); return 0; } break;
    case WM_DESTROY: { g_running = 0; PostQuitMessage(0); return 0; } break;
    case WM_QUIT: { g_running = 0; return 0 ; } break;
    case WM_SETFOCUS: { g_hasfocus = 1; } break;
    case WM_KILLFOCUS: { g_hasfocus = 0; } break;
    }

    return(DefWindowProc(window_handle, message, wparam, lparam));
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

internal_function float32_t measure_time_difference(LARGE_INTEGER begin_time, LARGE_INTEGER end_time, LARGE_INTEGER frequency)
{
    return float32_t(end_time.QuadPart - begin_time.QuadPart) / float32_t(frequency.QuadPart);
}

internal_function void init_free_list_allocator_head(free_list_allocator_t *allocator = &free_list_allocator_global)
{
    allocator->free_block_head = (free_block_header_t *)allocator->start;
    allocator->free_block_head->free_block_size = allocator->available_bytes;
}

// Actual game memory
global_var game_memory_t g_game;
// Game code
/*global_var struct game_code_t
{
    HMODULE game_code;
    const char *original_dir;
    const char *tmp_dir;
    FILETIME file_time;

    typedef void (*load_game_ptr_t)(game_memory_t *);
    typedef void (*initialize_game_ptr_t)(game_memory_t *, input_state_t *, create_vulkan_surface *);
    typedef void (*destroy_game_ptr_t)(game_memory_t *);
    typedef void (*game_tick_ptr_t)(game_memory_t *, input_state_t *, float32_t);
    
    load_game_ptr_t load_game_proc;
    initialize_game_ptr_t initialize_game_proc;
    destroy_game_ptr_t destroy_game_proc;
    game_tick_ptr_t game_tick_proc;
} g_game_code;*/

// TODO: Find a good way to do DLL hotloading
void load_game_from_dll(void)
{
    /*g_game_code.game_code = LoadLibraryA(g_game_code.original_dir);
    
    g_game_code.load_game_proc = (game_code_t::load_game_ptr_t)GetProcAddress(g_game_code.game_code, "load_game");
    g_game_code.initialize_game_proc = (game_code_t::initialize_game_ptr_t)GetProcAddress(g_game_code.game_code, "initialize_game");
    g_game_code.destroy_game_proc = (game_code_t::destroy_game_ptr_t)GetProcAddress(g_game_code.game_code, "destroy_game");
    g_game_code.game_tick_proc = (game_code_t::game_tick_ptr_t)GetProcAddress(g_game_code.game_code, "game_tick");*/
}

// TODO: Add this to server mode
void initialize_win32_console(void)
{
    char text[] = "Started session\n";
    if (WriteConsole(GetStdHandle(STD_OUTPUT_HANDLE), text, strlen(text), NULL, NULL) == FALSE)
    {
        if (AllocConsole() == TRUE)
        {
            WriteConsole(GetStdHandle(STD_OUTPUT_HANDLE), text, strlen(text), NULL, NULL);
        }
    }
}

void print_text_to_console(const char *string)
{
    WriteConsole(GetStdHandle(STD_OUTPUT_HANDLE), string, strlen(string), NULL, NULL);
}

int32_t CALLBACK WinMain(HINSTANCE hinstance, HINSTANCE prev_instance, LPSTR cmdline, int32_t showcmd)
{
    // Initialize game's dynamic memory
    linear_allocator_global.capacity = megabytes(30);
    linear_allocator_global.start = linear_allocator_global.current = malloc(linear_allocator_global.capacity);
	
    stack_allocator_global.capacity = megabytes(10);
    stack_allocator_global.start = stack_allocator_global.current = malloc(stack_allocator_global.capacity);

    free_list_allocator_global.available_bytes = megabytes(30);
    free_list_allocator_global.start = malloc(free_list_allocator_global.available_bytes);
    init_free_list_allocator_head(&free_list_allocator_global);

#if defined (SERVER_APPLICATION)
    application_type_t app_type = application_type_t::WINDOW_APPLICATION_MODE;
    application_mode_t app_mode = application_mode_t::SERVER_MODE;
#elif defined (CLIENT_APPLICATION)
    application_type_t app_type = application_type_t::WINDOW_APPLICATION_MODE;
    application_mode_t app_mode = application_mode_t::CLIENT_MODE;
#endif

    if (app_type == application_type_t::WINDOW_APPLICATION_MODE)
    {
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

        POINT top_left_of_window;
        top_left_of_window.x = 0;
        top_left_of_window.y = 0;
        ClientToScreen(g_window, &top_left_of_window);
    
        SetCursorPos(top_left_of_window.x + 2, top_left_of_window.y + 2);
        ShowWindow(g_window, showcmd);
        // TODO: ReleaseCapture(void) when alt tab / alt f4
        //SetCapture(g_window);
        disable_cursor_display();
    }

    // Loads function pointers into memory
    load_game_from_dll();

    // If in console mode, surface obviously won't get created
    create_vulkan_surface_win32 create_surface_proc_win32 = {};
    create_surface_proc_win32.window_ptr = &g_window;

    load_game(&g_game);
    initialize_game(&g_game, &g_input_state, &create_surface_proc_win32, app_mode, app_type);

    if (app_type == application_type_t::CONSOLE_APPLICATION_MODE)
    {
        initialize_win32_console();
    }
    
    g_running = 1;
    
    uint32_t sleep_granularity_milliseconds = 1;
    bool32_t success = (timeBeginPeriod(sleep_granularity_milliseconds) == TIMERR_NOERROR);

    LARGE_INTEGER clock_frequency;
    QueryPerformanceFrequency(&clock_frequency);
    
    while(g_running)
    {
        LARGE_INTEGER tick_start;
        QueryPerformanceCounter(&tick_start);

        switch (app_type)
        {
        case application_type_t::WINDOW_APPLICATION_MODE:
            {
                g_input_state.resized = 0;
                
                MSG message;
                while (PeekMessage(&message, NULL, 0, 0, PM_REMOVE))
                {
                    if (message.message == WM_QUIT)
                    {
                        g_running = false;
                    }
                    TranslateMessage(&message);
                    DispatchMessage(&message);
                }

                if (g_input_state.resized)
                {
                    handle_window_resize(&g_input_state);
                }

                RECT client_rect = {};
                GetClientRect(g_window, &client_rect);

                POINT top_left = { client_rect.left, client_rect.top }, bottom_right = { client_rect.right, client_rect.bottom };
                ClientToScreen(g_window, &top_left);
                ClientToScreen(g_window, &bottom_right);

                client_rect.left = top_left.x;
                client_rect.top = top_left.y;
                client_rect.right = bottom_right.x;
                client_rect.bottom = bottom_right.y;

                if (g_hasfocus) ClipCursor(&client_rect);
        
                // Render
                game_tick(&g_game, &g_input_state, g_dt);
                RedrawWindow(g_window, NULL, NULL, RDW_INTERNALPAINT);

                g_input_state.cursor_moved = false;

                g_input_state.char_count = 0;
                g_input_state.keyboard[keyboard_button_type_t::BACKSPACE].is_down = is_down_t::NOT_DOWN;
                g_input_state.keyboard[keyboard_button_type_t::ENTER].is_down = is_down_t::NOT_DOWN;
                g_input_state.keyboard[keyboard_button_type_t::ESCAPE].is_down = is_down_t::NOT_DOWN;
                g_input_state.keyboard[keyboard_button_type_t::LEFT_CONTROL].is_down = is_down_t::NOT_DOWN;
                // TODO: Set input state's normalized cursor position
            } break;
        case application_type_t::CONSOLE_APPLICATION_MODE:
            {
                game_tick(&g_game, &g_input_state, g_dt);
            } break;
        }
        
        clear_linear();

        LARGE_INTEGER tick_end;
        QueryPerformanceCounter(&tick_end);
        float32_t dt = measure_time_difference(tick_start, tick_end, clock_frequency);

        if (dt > TICK_TIME)
        {
            g_dt = dt;
            g_game_time += g_dt;
            g_input_state.dt = g_dt;
        }
        else
        {
            // Set game tick period by sleeping
            while (dt < TICK_TIME)
            {
                DWORD to_wait = DWORD( (TICK_TIME - dt) * 1000 );
                if (to_wait > 0)
                {
                    Sleep(to_wait);
                }

                LARGE_INTEGER now;
                QueryPerformanceCounter(&now);
                dt = measure_time_difference(tick_start, now, clock_frequency);
            }

            g_dt = TICK_TIME;
            g_game_time += g_dt;
            g_input_state.dt = g_dt;
        }
    }

    //ReleaseCapture();
    ShowWindow(g_window, SW_HIDE);
    
    destroy_game(&g_game);
    
    return(0);
}
