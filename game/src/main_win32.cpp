#define NOMINMAX

#define VK_USE_PLATFORM_WIN32_KHR

#include "core.hpp"
#include <stdlib.h>

#include "sk_game.hpp"

#define _WINSOCKAPI_
#include <Windows.h>
#include <Windowsx.h>

#define TICK_TIME 1.0f / 60.0f



// Global data
static HWND window;
static bool running;
static float32_t dt;
static raw_input_t raw_input;



// Static methods declarations
static LRESULT CALLBACK win32_callback(HWND window_handle, UINT message, WPARAM wparam, LPARAM lparam);
static void request_quit(void);
static void toggle_fullscreen(void);
static void handle_keyboard_event(WPARAM wparam, LPARAM lparam, int32_t action);
static void handle_mouse_move_event(LPARAM lparam);
static void set_key_state(keyboard_button_type_t button, int32_t action);
static void set_mouse_button_state(mouse_button_type_t button, int32_t action);
static void enter_fullscreen(void);
static void exit_fullscreen(void);
static float32_t measure_time_difference(LARGE_INTEGER begin_time, LARGE_INTEGER end_time, LARGE_INTEGER frequency);



// Entry point
int32_t CALLBACK WinMain(HINSTANCE hinstance, HINSTANCE prev_instance, LPSTR cmdline, int32_t showcmd)
{
    // Initialize game's dynamic memory
    linear_allocator_global.capacity = megabytes(30);
    linear_allocator_global.start = linear_allocator_global.current = malloc(linear_allocator_global.capacity);
	
    stack_allocator_global.capacity = megabytes(10);
    stack_allocator_global.start = stack_allocator_global.current = malloc(stack_allocator_global.capacity);

    free_list_allocator_global.available_bytes = megabytes(30);
    free_list_allocator_global.start = malloc(free_list_allocator_global.available_bytes);
    free_list_allocator_global.free_block_head = (free_block_header_t *)free_list_allocator_global.start;
    free_list_allocator_global.free_block_head->free_block_size = free_list_allocator_global.available_bytes;

#if defined (SERVER_APPLICATION)
    application_type_t app_type = application_type_t::WINDOW_APPLICATION_MODE;
    application_mode_t app_mode = application_mode_t::SERVER_MODE;
    const char *application_name = "Server";    
#elif defined (CLIENT_APPLICATION)
    application_type_t app_type = application_type_t::WINDOW_APPLICATION_MODE;
    application_mode_t app_mode = application_mode_t::CLIENT_MODE;
    const char *application_name = "Saska";
#endif

    if (app_type == application_type_t::WINDOW_APPLICATION_MODE)
    {
        const char *window_class_name = "saska_window_class";
        const char *window_name = application_name;

        WNDCLASS window_class = {};
        ZeroMemory(&window_class, sizeof(window_class));
        window_class.style = CS_OWNDC | CS_VREDRAW | CS_HREDRAW;
        window_class.lpfnWndProc = win32_callback;
        window_class.cbClsExtra = 0;
        window_class.hInstance = hinstance;
        window_class.hIcon = 0;
        window_class.hbrBackground = 0;
        window_class.lpszMenuName = 0;
        window_class.lpszClassName = window_class_name;

        assert(RegisterClass(&window_class));

        window = CreateWindowEx(0,
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

        ShowWindow(window, showcmd);
        
        hide_cursor();
    }

    // If in console mode, surface obviously won't get created
    //create_vulkan_surface_win32 create_surface_proc_win32 = {};
    //create_surface_proc_win32.window_ptr = &window;

    //initialize_game(&g_game, &raw_input, &create_surface_proc_win32, app_mode, app_type);
    
    running = 1;
    
    uint32_t sleep_granularity_milliseconds = 1;
    bool32_t success = (timeBeginPeriod(sleep_granularity_milliseconds) == TIMERR_NOERROR);

    LARGE_INTEGER clock_frequency;
    QueryPerformanceFrequency(&clock_frequency);
    
    while(running)
    {
        LARGE_INTEGER tick_start;
        QueryPerformanceCounter(&tick_start);

        raw_input.resized = 0;
                
        MSG message;
        while (PeekMessage(&message, NULL, 0, 0, PM_REMOVE))
        {
            if (message.message == WM_QUIT)
            {
                running = false;
            }
            TranslateMessage(&message);
            DispatchMessage(&message);
        }

        if (raw_input.resized)
        {
            handle_window_resize(&raw_input);
        }
        
        // Render
        //game_tick(&g_game, &raw_input, dt);
        RedrawWindow(window, NULL, NULL, RDW_INTERNALPAINT);

        raw_input.cursor_moved = false;

        raw_input.char_count = 0;
        raw_input.keyboard[keyboard_button_type_t::BACKSPACE].state = button_state_t::NOT_DOWN;
        raw_input.keyboard[keyboard_button_type_t::ENTER].state = button_state_t::NOT_DOWN;
        raw_input.keyboard[keyboard_button_type_t::ESCAPE].state = button_state_t::NOT_DOWN;
        raw_input.keyboard[keyboard_button_type_t::LEFT_CONTROL].state = button_state_t::NOT_DOWN;
        
        clear_linear();

        LARGE_INTEGER tick_end;
        QueryPerformanceCounter(&tick_end);
        dt = measure_time_difference(tick_start, tick_end, clock_frequency);

        if (dt < TICK_TIME)
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

            dt = TICK_TIME;
        }
    }

    ShowWindow(window, SW_HIDE);
    
    return(0);
}

static LRESULT CALLBACK win32_callback(HWND window_handle, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch(message)
    {
        // Input ...
    case WM_SIZE:
        {
            raw_input.resized = 1;
            raw_input.window_width = LOWORD(lparam);
            raw_input.window_height = HIWORD(lparam);
        } break;
    case WM_MOUSEMOVE: { if (raw_input.window_has_focus) handle_mouse_move_event(lparam); } break;
    case WM_KEYDOWN: { if (raw_input.window_has_focus) handle_keyboard_event(wparam, lparam, key_action_t::KEY_ACTION_DOWN); } break;
    case WM_CHAR:
        {
            if (raw_input.char_count != MAX_PRESSED_CHARS_PER_FRAME && raw_input.window_has_focus)
            {
                if (wparam >= 32)
                {
                    raw_input.char_stack[raw_input.char_count++] = (char)wparam;
                }
            }
        } break;
    case WM_KEYUP: { if (raw_input.window_has_focus) handle_keyboard_event(wparam, lparam, key_action_t::KEY_ACTION_UP); } break;
    case WM_RBUTTONDOWN: { if (raw_input.window_has_focus) set_mouse_button_state(mouse_button_type_t::MOUSE_RIGHT, key_action_t::KEY_ACTION_DOWN); } break;
    case WM_RBUTTONUP: { if (raw_input.window_has_focus) set_mouse_button_state(mouse_button_type_t::MOUSE_RIGHT, key_action_t::KEY_ACTION_UP); } break;
    case WM_LBUTTONDOWN: { if (raw_input.window_has_focus) set_mouse_button_state(mouse_button_type_t::MOUSE_LEFT, key_action_t::KEY_ACTION_DOWN); } break;
    case WM_LBUTTONUP: { if (raw_input.window_has_focus) set_mouse_button_state(mouse_button_type_t::MOUSE_LEFT, key_action_t::KEY_ACTION_UP); } break;
        // NOTE: Make sure this doesn't break anything in the future
        //case WM_SETCURSOR: { SetCursor(0);} break;
    case WM_CLOSE: { running = 0; PostQuitMessage(0); return 0; } break;
    case WM_DESTROY: { running = 0; PostQuitMessage(0); return 0; } break;
    case WM_QUIT: { running = 0; return 0 ; } break;
    case WM_SETFOCUS: { raw_input.window_has_focus = 1; } break;
    case WM_KILLFOCUS: { raw_input.window_has_focus = 0; } break;
    }
    
    return(DefWindowProc(window_handle, message, wparam, lparam));
}

static void request_quit(void)
{
    running = 0;
    PostQuitMessage(0);
}

static void toggle_fullscreen(void)
{
    if (!raw_input.toggled_fullscreen)
    {
        raw_input.toggled_fullscreen = 1;

        if (raw_input.in_fullscreen)
        {
            raw_input.in_fullscreen = 0;
            exit_fullscreen();
        }
        else
        {
            raw_input.in_fullscreen = 1;
            enter_fullscreen();
        }
    }
}

static void handle_keyboard_event(WPARAM wparam, LPARAM lparam, int32_t action)
{
    switch(wparam)
    {
    case 0x41: { set_key_state(keyboard_button_type_t::A, action); } break;
    case 0x42: { set_key_state(keyboard_button_type_t::B, action); } break;
    case 0x43: { set_key_state(keyboard_button_type_t::C, action); } break;
    case 0x44: { set_key_state(keyboard_button_type_t::D, action); } break;
    case 0x45: { set_key_state(keyboard_button_type_t::E, action); } break;
    case 0x46: { set_key_state(keyboard_button_type_t::F, action); } break;
    case 0x47: { set_key_state(keyboard_button_type_t::G, action); } break;
    case 0x48: { set_key_state(keyboard_button_type_t::H, action); } break;
    case 0x49: { set_key_state(keyboard_button_type_t::I, action); } break;
    case 0x4A: { set_key_state(keyboard_button_type_t::J, action); } break;
    case 0x4B: { set_key_state(keyboard_button_type_t::K, action); } break;
    case 0x4C: { set_key_state(keyboard_button_type_t::L, action); } break;
    case 0x4D: { set_key_state(keyboard_button_type_t::M, action); } break;
    case 0x4E: { set_key_state(keyboard_button_type_t::N, action); } break;
    case 0x4F: { set_key_state(keyboard_button_type_t::O, action); } break;
    case 0x50: { set_key_state(keyboard_button_type_t::P, action); } break;
    case 0x51: { set_key_state(keyboard_button_type_t::Q, action); } break;
    case 0x52: { set_key_state(keyboard_button_type_t::R, action); } break;
    case 0x53: { set_key_state(keyboard_button_type_t::S, action); } break;
    case 0x54: { set_key_state(keyboard_button_type_t::T, action); } break;
    case 0x55: { set_key_state(keyboard_button_type_t::U, action); } break;
    case 0x56: { set_key_state(keyboard_button_type_t::V, action); } break;
    case 0x57: { set_key_state(keyboard_button_type_t::W, action); } break;
    case 0x58: { set_key_state(keyboard_button_type_t::X, action); } break;
    case 0x59: { set_key_state(keyboard_button_type_t::Y, action); } break;
    case 0x5A: { set_key_state(keyboard_button_type_t::Z, action); } break;
    case 0x30: { set_key_state(keyboard_button_type_t::ZERO, action); } break;
    case 0x31: { set_key_state(keyboard_button_type_t::ONE, action); } break;
    case 0x32: { set_key_state(keyboard_button_type_t::TWO, action); } break;
    case 0x33: { set_key_state(keyboard_button_type_t::THREE, action); } break;
    case 0x34: { set_key_state(keyboard_button_type_t::FOUR, action); } break;
    case 0x35: { set_key_state(keyboard_button_type_t::FIVE, action); } break;
    case 0x36: { set_key_state(keyboard_button_type_t::SIX, action); } break;
    case 0x37: { set_key_state(keyboard_button_type_t::SEVEN, action); } break;
    case 0x38: { set_key_state(keyboard_button_type_t::EIGHT, action); } break;
    case 0x39: { set_key_state(keyboard_button_type_t::NINE, action); } break;
    case VK_UP: { set_key_state(keyboard_button_type_t::UP, action); } break;
    case VK_LEFT: { set_key_state(keyboard_button_type_t::LEFT, action); } break;
    case VK_DOWN: { set_key_state(keyboard_button_type_t::DOWN, action); } break;
    case VK_RIGHT: { set_key_state(keyboard_button_type_t::RIGHT, action); } break;
    case VK_SPACE: { set_key_state(keyboard_button_type_t::SPACE, action); } break;
    case VK_SHIFT: { set_key_state(keyboard_button_type_t::LEFT_SHIFT, action); } break;
    case VK_CONTROL: { set_key_state(keyboard_button_type_t::LEFT_CONTROL, action); } break;
    case VK_RETURN: { set_key_state(keyboard_button_type_t::ENTER, action); } break;
    case VK_BACK: { set_key_state(keyboard_button_type_t::BACKSPACE, action); } break;
    case VK_ESCAPE: { set_key_state(keyboard_button_type_t::ESCAPE, action); } break;
    case VK_F11: { if (action == key_action_t::KEY_ACTION_UP) raw_input.toggled_fullscreen = 0; else toggle_fullscreen(); } break;
    }
}

static void handle_mouse_move_event(LPARAM lparam)
{
    static int32_t true_prev_x = 0;
    static int32_t true_prev_y = 0;
 
    RECT client_rect = {};
    GetClientRect(window, &client_rect);

    POINT cursor_pos = {};
    GetCursorPos(&cursor_pos);
    ScreenToClient(window, &cursor_pos);
    
    // In client space
    int32_t true_x_position = cursor_pos.x;
    int32_t true_y_position = cursor_pos.y;
    
    if (true_x_position != true_prev_x || true_y_position != true_prev_y)
    {
        raw_input.cursor_moved = true;
        raw_input.previous_cursor_pos_x = raw_input.cursor_pos_x;
        raw_input.previous_cursor_pos_y = raw_input.cursor_pos_y;
    }
    
    int32_t true_diff_x = true_x_position - true_prev_x;
    int32_t true_diff_y = true_y_position - true_prev_y;

    // Virtual "infinite" cursor space
    raw_input.cursor_pos_x += (float32_t)true_diff_x;
    raw_input.cursor_pos_y += (float32_t)true_diff_y;
            
    true_prev_x = true_x_position;
    true_prev_y = true_y_position;

    POINT top_left = { client_rect.left, client_rect.top }, bottom_right = { client_rect.right, client_rect.bottom };
    ClientToScreen(window, &top_left);
    ClientToScreen(window, &bottom_right);

    client_rect.left = top_left.x;
    client_rect.top = top_left.y;
    client_rect.right = bottom_right.x;
    client_rect.bottom = bottom_right.y;

    POINT current_position = {};
    
    if (!raw_input.is_showing_cursor)
    {
        ClientToScreen(window, &current_position);
        
        ClipCursor(&client_rect);
        current_position.x += client_rect.right / 2;
        current_position.y += client_rect.bottom / 2;
        SetCursorPos(current_position.x, current_position.y);
    }
    else
    {
        current_position.x = true_x_position;
        current_position.y = true_y_position;
    }

    ScreenToClient(window, &current_position);
    
    true_prev_x = current_position.x;
    true_prev_y = current_position.y;
}

static void set_key_state(keyboard_button_type_t button, int32_t action)
{
    if (action == key_action_t::KEY_ACTION_DOWN)
    {
        keyboard_button_t *key = &raw_input.keyboard[button];
        switch(key->state)
        {
        case button_state_t::NOT_DOWN: {key->state = button_state_t::INSTANT;} break;
        case button_state_t::INSTANT: {key->state = button_state_t::REPEAT;} break;
        }
        key->down_amount += dt;
    }
    else if (action == key_action_t::KEY_ACTION_UP)
    {
        keyboard_button_t *key = &raw_input.keyboard[button];
        key->state = button_state_t::NOT_DOWN;
        key->down_amount = 0.0f;
    }
}

static void set_mouse_button_state(mouse_button_type_t button, int32_t action)
{
    if (action == key_action_t::KEY_ACTION_DOWN)
    {
        mouse_button_t *mouse_button = &raw_input.mouse_buttons[button];
        switch(mouse_button->state)
        {
        case button_state_t::NOT_DOWN: {mouse_button->state = button_state_t::INSTANT;} break;
        case button_state_t::INSTANT: case button_state_t::REPEAT: {mouse_button->state = button_state_t::REPEAT;} break;
        }
        mouse_button->down_amount += dt;
    }
    else if (action == key_action_t::KEY_ACTION_UP)
    {
        mouse_button_t *mouse_button = &raw_input.mouse_buttons[button];
        mouse_button->state = button_state_t::NOT_DOWN;
        mouse_button->down_amount = 0.0f;
    }
}

static void enter_fullscreen(void)
{
    POINT point = {0};
    HMONITOR monitor = MonitorFromPoint(point, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitor_info = { sizeof(monitor_info) };
    if (GetMonitorInfo(monitor, &monitor_info))
    {
        DWORD style = WS_POPUP | WS_VISIBLE;
        SetWindowLongPtr(window, GWL_STYLE, style);
        SetWindowPos(window, 0, monitor_info.rcMonitor.left, monitor_info.rcMonitor.top,
                     monitor_info.rcMonitor.right - monitor_info.rcMonitor.left, monitor_info.rcMonitor.bottom - monitor_info.rcMonitor.top,
                     SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        raw_input.resized = 1;
        raw_input.window_width = monitor_info.rcMonitor.right - monitor_info.rcMonitor.left;
        raw_input.window_height = monitor_info.rcMonitor.bottom - monitor_info.rcMonitor.top;
    }
}

static void exit_fullscreen(void)
{
    // TODO:
}

static float32_t measure_time_difference(LARGE_INTEGER begin_time, LARGE_INTEGER end_time, LARGE_INTEGER frequency)
{
    return float32_t(end_time.QuadPart - begin_time.QuadPart) / float32_t(frequency.QuadPart);
}



// Delared in platform.hpp
void show_cursor(void)
{
    ShowCursor(1);
    raw_input.is_showing_cursor = 1;
}

void hide_cursor(void)
{
    ShowCursor(0);
    raw_input.is_showing_cursor = 0;
}

// Win32 Debug Console
void output_to_debug_console_i(int32_t i)
{
    char buffer[15] = {};
    sprintf(buffer, "%i\0", i);
    OutputDebugString(buffer);
}

void output_to_debug_console_i(float32_t f)
{
    char buffer[15] = {};
    sprintf(buffer, "%f\0", f);
    OutputDebugString(buffer);
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
    OutputDebugString(string);
}
