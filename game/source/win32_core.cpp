/* win32_core.cpp */
// TODO: NEED TO REFACTOR GAME SO THAT EVERYTHING IS SPLIT INTO MORE READABLE FILES (e.g. console.hpp/cpp, voxel_chunk.hpp/cpp, etc...)

#define NOMINMAX

#define VK_USE_PLATFORM_WIN32_KHR
// Empty
#define MEMORY_API
#define SOURCE_GAME

#define _WINSOCKAPI_ 

#if defined (UNITY_BUILD)

#include "allocators.cpp"
#include "bullet.cpp"
#include "camera.cpp"
#include "camera_view.cpp"
#include "chunk.cpp"
#include "chunks_gstate.cpp"
#include "client.cpp"
#include "collision.cpp"
#include "component.cpp"
#include "containers.hpp"
#include "core.cpp"
#include "entities_gstate.cpp"
#include "file_system.cpp"
#include "game.cpp"
#include "game_input.cpp"
#include "gamestate.cpp"
#include "graphics.cpp"
#include "memory.cpp"
#include "net.cpp"
#include "particles_gstate.cpp"
#include "player.cpp"
#include "script.cpp"
#include "serializer.cpp"
#include "server.cpp"
#include "sockets.cpp"
#include "thread_pool.cpp"
#include "ui.cpp"
#include "utility.hpp"
#include "vulkan.cpp"

#else

#include "thread_pool.hpp"
#include "memory.hpp"
#include "vulkan.hpp"
#include <stdlib.h>

#include "game.hpp"
#include "vulkan.hpp"

#include <Windows.h>
#include <Windowsx.h>

#include <xinput.h>

#include "raw_input.hpp"

#endif


// Global
static bool running;
static double dt = 0.0f;
static HWND window;
static raw_input_t raw_input = {};
static game_memory_t game;



// Static declarations
static void set_mouse_move_button_state(float32_t value, bool active);
static void handle_mouse_move_event(LPARAM lparam);
enum key_action_t { KEY_ACTION_DOWN, KEY_ACTION_UP };
static void set_key_state(raw_input_t *raw_input, button_type_t button, int32_t action);
static void set_mouse_button_state(raw_input_t *raw_input, button_type_t button, int32_t action);
static void set_gamepad_button_state(raw_input_t *raw_input, gamepad_button_type_t button, bool active, float32_t value = 1.0f);
static void enter_fullscreen(void);
static void exit_fullscreen(void);
static void toggle_fullscreen(void);
static void handle_keyboard_event(WPARAM wparam, LPARAM lparam, int32_t action);
static LRESULT CALLBACK win32_callback(HWND window_handle, UINT message, WPARAM wparam, LPARAM lparam);
static void get_gamepad_state(void);
static float32_t measure_time_difference(LARGE_INTEGER begin_time, LARGE_INTEGER end_time, LARGE_INTEGER frequency);
static void init_free_list_allocator_head(free_list_allocator_t *allocator = &free_list_allocator_global);
static void parse_command_line_args(LPSTR cmdline, application_type_t *app_type, application_mode_t *app_mode, const char **application_name);

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


void send_vibration_to_gamepad(void)
{
    // Get gamepad input state and active controller
    int32_t active_controller = -1;
    XINPUT_STATE gamepad_state = {};
    ZeroMemory(&gamepad_state, sizeof(gamepad_state));
                
    for (uint32_t i = 0; i < XUSER_MAX_COUNT; ++i)
    {
        DWORD result = XInputGetState(i, &gamepad_state);
        if (result == ERROR_SUCCESS)
        {
            // Connected
            active_controller = i;
            break;
        }
    }
    
    XINPUT_VIBRATION vibration;
    ZeroMemory( &vibration, sizeof(XINPUT_VIBRATION) );
    vibration.wLeftMotorSpeed = 32000; // use any value between 0-65535 here
    vibration.wRightMotorSpeed = 16000; // use any value between 0-65535 here
    XInputSetState( active_controller, &vibration );
}



// Win32 entry point
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


    application_type_t app_type;
    application_mode_t app_mode;
    const char *application_name;
    parse_command_line_args(cmdline, &app_type, &app_mode, &application_name);


    if (app_type == application_type_t::WINDOW_APPLICATION_MODE)
    {
        SetProcessDPIAware();
        
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

        POINT top_left_of_window;
        top_left_of_window.x = 0;
        top_left_of_window.y = 0;
        ClientToScreen(window, &top_left_of_window);
    
        SetCursorPos(top_left_of_window.x + 2, top_left_of_window.y + 2);
        ShowWindow(window, showcmd);
        
        disable_cursor_display();
    }

    // If in console mode, surface obviously won't get created
    create_vulkan_surface_win32 create_surface_proc_win32 = {};
    create_surface_proc_win32.window_ptr = &window;

    load_game(&game);
    initialize_game(&game, &raw_input, &create_surface_proc_win32, app_mode, app_type);
    
    running = 1;
    
    uint32_t sleep_granularity_milliseconds = 1;
    bool32_t success = (timeBeginPeriod(sleep_granularity_milliseconds) == TIMERR_NOERROR);

    LARGE_INTEGER clock_frequency;
    QueryPerformanceFrequency(&clock_frequency);
    
    while(running)
    {
        LARGE_INTEGER tick_start;
        QueryPerformanceCounter(&tick_start);

        switch (app_type)
        {
        case application_type_t::WINDOW_APPLICATION_MODE:
            {
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


                get_gamepad_state();

                
                if (raw_input.resized)
                {
                    handle_window_resize(&raw_input);
                }

                /*RECT client_rect = {};
                GetClientRect(window, &client_rect);

                POINT top_left = { client_rect.left, client_rect.top }, bottom_right = { client_rect.right, client_rect.bottom };
                ClientToScreen(window, &top_left);
                ClientToScreen(window, &bottom_right);

                client_rect.left = top_left.x;
                client_rect.top = top_left.y;
                client_rect.right = bottom_right.x - 2;
                client_rect.bottom = bottom_right.y - 2;

                if (raw_input.has_focus)
                {
                    ClipCursor(&client_rect);
                }*/
        
                // Render
                game_tick(&game, &raw_input, dt);
                RedrawWindow(window, NULL, NULL, RDW_INTERNALPAINT);

                raw_input.cursor_moved = false;
                raw_input.buttons[button_type_t::MOUSE_MOVE_RIGHT].state = button_state_t::NOT_DOWN;
                raw_input.buttons[button_type_t::MOUSE_MOVE_LEFT].state = button_state_t::NOT_DOWN;
                raw_input.buttons[button_type_t::MOUSE_MOVE_UP].state = button_state_t::NOT_DOWN;
                raw_input.buttons[button_type_t::MOUSE_MOVE_DOWN].state = button_state_t::NOT_DOWN;
                raw_input.buttons[button_type_t::MOUSE_MOVE_RIGHT].value = 0;
                raw_input.buttons[button_type_t::MOUSE_MOVE_LEFT].value = 0;
                raw_input.buttons[button_type_t::MOUSE_MOVE_UP].value = 0;
                raw_input.buttons[button_type_t::MOUSE_MOVE_DOWN].value = 0;

                raw_input.char_count = 0;
                raw_input.buttons[button_type_t::BACKSPACE].state = button_state_t::NOT_DOWN;
                raw_input.buttons[button_type_t::ENTER].state = button_state_t::NOT_DOWN;
                raw_input.buttons[button_type_t::ESCAPE].state = button_state_t::NOT_DOWN;
                raw_input.buttons[button_type_t::LEFT_CONTROL].state = button_state_t::NOT_DOWN;
                // TODO: Set input state's normalized cursor position
            } break;
        case application_type_t::CONSOLE_APPLICATION_MODE:
            {
                game_tick(&game, &raw_input, dt);
            } break;
        }
        
        clear_linear();

        LARGE_INTEGER tick_end;
        QueryPerformanceCounter(&tick_end);
        float32_t new_dt = measure_time_difference(tick_start, tick_end, clock_frequency);

        
        // Disable this for debugging
        if (new_dt > TICK_TIME)
        {
            dt = new_dt;
            raw_input.dt = new_dt;
        }
        else
        {
            // Set game tick period by sleeping
            while (new_dt < TICK_TIME)
            {
                DWORD to_wait = DWORD( (TICK_TIME - new_dt) * 1000 );
                if (to_wait > 0)
                {
                    Sleep(to_wait);
                }

                LARGE_INTEGER now;
                QueryPerformanceCounter(&now);
                new_dt = measure_time_difference(tick_start, now, clock_frequency);
            }

            dt = TICK_TIME;
            raw_input.dt = dt;
        }
    }

    //ReleaseCapture();
    ShowWindow(window, SW_HIDE);
    
    destroy_game(&game);
    
    return(0);
}


static void set_mouse_move_button_state(float32_t value, bool active, button_type_t button)
{
    if (active)
    {
        raw_input.buttons[button].state = (button_state_t)1;
        raw_input.buttons[button].down_amount += dt;
        raw_input.buttons[button].value = value;
    }
    else
    {
        raw_input.buttons[button].state = (button_state_t)0;
        raw_input.buttons[button].down_amount = 0;
        raw_input.buttons[button].value = 0;
    }
}


// Static definitions
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


    // Calculate difference
    int32_t dx = raw_input.cursor_pos_x - raw_input.previous_cursor_pos_x;
    int32_t dy = raw_input.cursor_pos_y - raw_input.previous_cursor_pos_y;

    //output_to_debug_console(dx, " ", dy, "\n");
    
    vector2_t d = vector2_t(0.0f);
    if (dx != 0 || dy != 0)
    {
        d = vector2_t(dx, dy);
    }

    if (raw_input.cursor_moved)
    {
        set_mouse_move_button_state(d.x, dx > 0, button_type_t::MOUSE_MOVE_RIGHT);
        set_mouse_move_button_state(d.x, dx < 0, button_type_t::MOUSE_MOVE_LEFT);
        set_mouse_move_button_state(d.y, dy > 0, button_type_t::MOUSE_MOVE_UP);
        set_mouse_move_button_state(d.y, dy < 0, button_type_t::MOUSE_MOVE_DOWN);
    }
    
            
    true_prev_x = true_x_position;
    true_prev_y = true_y_position;
    
    /*if (true_x_position >= client_rect.right - 2)
    {
        POINT infinite = { 2, true_y_position };
        true_x_position = true_prev_x = 2;
        ClientToScreen(window, &infinite);
        SetCursorPos(infinite.x, infinite.y);
    }
    if (true_x_position <= client_rect.left + 1)
    {
        POINT infinite = { client_rect.right - 2, true_y_position };
        true_x_position = true_prev_x = client_rect.right - 2;
        ClientToScreen(window, &infinite);
        SetCursorPos(infinite.x, infinite.y);
    }
    if (true_y_position >= client_rect.bottom - 2)
    {
        POINT infinite = { true_x_position, client_rect.top + 2 };
        true_y_position = true_prev_y = client_rect.top + 2;
        ClientToScreen(window, &infinite);
        SetCursorPos(infinite.x, infinite.y);
    }
    if (true_y_position <= client_rect.top + 1)
    {
        POINT infinite = { true_x_position, client_rect.bottom - 2 };
        true_y_position = true_prev_y = client_rect.bottom - 2;
        ClientToScreen(window, &infinite);
        SetCursorPos(infinite.x, infinite.y);
        }*/

    POINT top_left = { client_rect.left, client_rect.top }, bottom_right = { client_rect.right, client_rect.bottom };
    ClientToScreen(window, &top_left);
    ClientToScreen(window, &bottom_right);

    client_rect.left = top_left.x;
    client_rect.top = top_left.y;
    client_rect.right = bottom_right.x;
    client_rect.bottom = bottom_right.y;

    //ClipCursor(&client_rect);

    POINT top_left_corner = {};
    ClientToScreen(window, &top_left_corner);
    top_left_corner.x += client_rect.right / 2;
    top_left_corner.y += client_rect.bottom / 2;
    if (!raw_input.show_cursor)
    {
        SetCursorPos(top_left_corner.x, top_left_corner.y);
    }

    ScreenToClient(window, &top_left_corner);
    
    true_prev_x = top_left_corner.x;
    true_prev_y = top_left_corner.y;
}


static void set_key_state(raw_input_t *raw_input, button_type_t button, int32_t action)
{
    if (action == key_action_t::KEY_ACTION_DOWN)
    {
        button_input_t *key = &raw_input->buttons[button];
        switch(key->state)
        {
        case button_state_t::NOT_DOWN: {key->state = button_state_t::INSTANT;} break;
        case button_state_t::INSTANT: {key->state = button_state_t::REPEAT;} break;
        }
        key->down_amount += dt;
        key->value = 1.0f;
    }
    else if (action == key_action_t::KEY_ACTION_UP)
    {
        button_input_t *key = &raw_input->buttons[button];
        key->state = button_state_t::NOT_DOWN;
        key->down_amount = 0.0f;
        key->value = 0.0f;
    }
}


static void set_mouse_button_state(raw_input_t *raw_input, button_type_t button, int32_t action)
{
    if (action == key_action_t::KEY_ACTION_DOWN)
    {
        button_input_t *mouse_button = &raw_input->buttons[button];
        switch(mouse_button->state)
        {
        case button_state_t::NOT_DOWN: {mouse_button->state = button_state_t::INSTANT;} break;
        case button_state_t::INSTANT: case button_state_t::REPEAT: {mouse_button->state = button_state_t::REPEAT;} break;
        }
        mouse_button->down_amount += dt;
        mouse_button->value = 0.0f;
    }
    else if (action == key_action_t::KEY_ACTION_UP)
    {
        button_input_t *mouse_button = &raw_input->buttons[button];
        mouse_button->state = button_state_t::NOT_DOWN;
        mouse_button->down_amount = 0.0f;
        mouse_button->value = 0.0f;
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
    case 0x41: { set_key_state(&raw_input, button_type_t::A, action); } break;
    case 0x42: { set_key_state(&raw_input, button_type_t::B, action); } break;
    case 0x43: { set_key_state(&raw_input, button_type_t::C, action); } break;
    case 0x44: { set_key_state(&raw_input, button_type_t::D, action); } break;
    case 0x45: { set_key_state(&raw_input, button_type_t::E, action); } break;
    case 0x46: { set_key_state(&raw_input, button_type_t::F, action); } break;
    case 0x47: { set_key_state(&raw_input, button_type_t::G, action); } break;
    case 0x48: { set_key_state(&raw_input, button_type_t::H, action); } break;
    case 0x49: { set_key_state(&raw_input, button_type_t::I, action); } break;
    case 0x4A: { set_key_state(&raw_input, button_type_t::J, action); } break;
    case 0x4B: { set_key_state(&raw_input, button_type_t::K, action); } break;
    case 0x4C: { set_key_state(&raw_input, button_type_t::L, action); } break;
    case 0x4D: { set_key_state(&raw_input, button_type_t::M, action); } break;
    case 0x4E: { set_key_state(&raw_input, button_type_t::N, action); } break;
    case 0x4F: { set_key_state(&raw_input, button_type_t::O, action); } break;
    case 0x50: { set_key_state(&raw_input, button_type_t::P, action); } break;
    case 0x51: { set_key_state(&raw_input, button_type_t::Q, action); } break;
    case 0x52: { set_key_state(&raw_input, button_type_t::R, action); } break;
    case 0x53: { set_key_state(&raw_input, button_type_t::S, action); } break;
    case 0x54: { set_key_state(&raw_input, button_type_t::T, action); } break;
    case 0x55: { set_key_state(&raw_input, button_type_t::U, action); } break;
    case 0x56: { set_key_state(&raw_input, button_type_t::V, action); } break;
    case 0x57: { set_key_state(&raw_input, button_type_t::W, action); } break;
    case 0x58: { set_key_state(&raw_input, button_type_t::X, action); } break;
    case 0x59: { set_key_state(&raw_input, button_type_t::Y, action); } break;
    case 0x5A: { set_key_state(&raw_input, button_type_t::Z, action); } break;
    case 0x30: { set_key_state(&raw_input, button_type_t::ZERO, action); } break;
    case 0x31: { set_key_state(&raw_input, button_type_t::ONE, action); } break;
    case 0x32: { set_key_state(&raw_input, button_type_t::TWO, action); } break;
    case 0x33: { set_key_state(&raw_input, button_type_t::THREE, action); } break;
    case 0x34: { set_key_state(&raw_input, button_type_t::FOUR, action); } break;
    case 0x35: { set_key_state(&raw_input, button_type_t::FIVE, action); } break;
    case 0x36: { set_key_state(&raw_input, button_type_t::SIX, action); } break;
    case 0x37: { set_key_state(&raw_input, button_type_t::SEVEN, action); } break;
    case 0x38: { set_key_state(&raw_input, button_type_t::EIGHT, action); } break;
    case 0x39: { set_key_state(&raw_input, button_type_t::NINE, action); } break;
    case VK_UP: { set_key_state(&raw_input, button_type_t::UP, action); } break;
    case VK_LEFT: { set_key_state(&raw_input, button_type_t::LEFT, action); } break;
    case VK_DOWN: { set_key_state(&raw_input, button_type_t::DOWN, action); } break;
    case VK_RIGHT: { set_key_state(&raw_input, button_type_t::RIGHT, action); } break;
    case VK_SPACE: { set_key_state(&raw_input, button_type_t::SPACE, action); } break;
    case VK_SHIFT: { set_key_state(&raw_input, button_type_t::LEFT_SHIFT, action); } break;
    case VK_CONTROL: { set_key_state(&raw_input, button_type_t::LEFT_CONTROL, action); } break;
    case VK_RETURN: { set_key_state(&raw_input, button_type_t::ENTER, action); } break;
    case VK_BACK: { set_key_state(&raw_input, button_type_t::BACKSPACE, action); } break;
    case VK_ESCAPE: { set_key_state(&raw_input, button_type_t::ESCAPE, action); } break;
    case VK_F11: { if (action == key_action_t::KEY_ACTION_UP) raw_input.toggled_fullscreen = 0; else toggle_fullscreen(); } break;
    }
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
    case WM_MOUSEMOVE: { if (raw_input.has_focus) handle_mouse_move_event(lparam); } break;
    case WM_KEYDOWN: { if (raw_input.has_focus) handle_keyboard_event(wparam, lparam, key_action_t::KEY_ACTION_DOWN); } break;
    case WM_CHAR:
        {
            if (raw_input.char_count != MAX_CHARS && raw_input.has_focus)
            {
                if (wparam >= 32)
                {
                    raw_input.char_stack[raw_input.char_count++] = (char)wparam;
                }
            }
        } break;
    case WM_KEYUP: { if (raw_input.has_focus) handle_keyboard_event(wparam, lparam, key_action_t::KEY_ACTION_UP); } break;
    case WM_RBUTTONDOWN: { if (raw_input.has_focus) set_mouse_button_state(&raw_input, button_type_t::MOUSE_RIGHT, key_action_t::KEY_ACTION_DOWN); } break;
    case WM_RBUTTONUP: { if (raw_input.has_focus) set_mouse_button_state(&raw_input, button_type_t::MOUSE_RIGHT, key_action_t::KEY_ACTION_UP); } break;
    case WM_LBUTTONDOWN: { if (raw_input.has_focus) set_mouse_button_state(&raw_input, button_type_t::MOUSE_LEFT, key_action_t::KEY_ACTION_DOWN); } break;
    case WM_LBUTTONUP: { if (raw_input.has_focus) set_mouse_button_state(&raw_input, button_type_t::MOUSE_LEFT, key_action_t::KEY_ACTION_UP); } break;
        // NOTE: Make sure this doesn't break anything in the future
    case WM_SETCURSOR: { SetCursor(0);} break;
    case WM_CLOSE: { running = 0; PostQuitMessage(0); return 0; } break;
    case WM_DESTROY: { running = 0; PostQuitMessage(0); return 0; } break;
    case WM_QUIT: { running = 0; return 0 ; } break;
    case WM_SETFOCUS: { raw_input.has_focus = 1; } break;
    case WM_KILLFOCUS: { raw_input.has_focus = 0; } break;
    }

    return(DefWindowProc(window_handle, message, wparam, lparam));
}


static void set_gamepad_button_state(raw_input_t *raw_input, gamepad_button_type_t button, bool active, float32_t value)
{
    if (active)
    {
        raw_input->gamepad_buttons[button].down_amount += dt;
        raw_input->gamepad_buttons[button].state = (button_state_t)1;
        raw_input->gamepad_buttons[button].value = value;
    }
    else
    {
        raw_input->gamepad_buttons[button].down_amount = 0.0f;
        raw_input->gamepad_buttons[button].state = (button_state_t)0;
        raw_input->gamepad_buttons[button].value = 0.0f;
    }
}


static void get_gamepad_state(void)
{
    // Get gamepad input state and active controller
    int32_t active_controller = -1;
    XINPUT_STATE gamepad_state = {};
    ZeroMemory(&gamepad_state, sizeof(gamepad_state));
                
    for (uint32_t i = 0; i < XUSER_MAX_COUNT; ++i)
    {
        DWORD result = XInputGetState(i, &gamepad_state);
        if (result == ERROR_SUCCESS)
        {
            // Connected
            active_controller = i;
            break;
        }
    }

    if (active_controller >= 0)
    {
        XINPUT_GAMEPAD *gamepad = &gamepad_state.Gamepad;
        raw_input.gamepad_packet_number = gamepad_state.dwPacketNumber;

        set_gamepad_button_state(&raw_input, gamepad_button_type_t::DPAD_UP, gamepad->wButtons & XINPUT_GAMEPAD_DPAD_UP);
        set_gamepad_button_state(&raw_input, gamepad_button_type_t::DPAD_DOWN, gamepad->wButtons & XINPUT_GAMEPAD_DPAD_DOWN);
        set_gamepad_button_state(&raw_input, gamepad_button_type_t::DPAD_LEFT, gamepad->wButtons & XINPUT_GAMEPAD_DPAD_LEFT);
        set_gamepad_button_state(&raw_input, gamepad_button_type_t::DPAD_RIGHT, gamepad->wButtons & XINPUT_GAMEPAD_DPAD_RIGHT);
        set_gamepad_button_state(&raw_input, gamepad_button_type_t::START, gamepad->wButtons & XINPUT_GAMEPAD_START);
        set_gamepad_button_state(&raw_input, gamepad_button_type_t::BACK, gamepad->wButtons & XINPUT_GAMEPAD_BACK);
        set_gamepad_button_state(&raw_input, gamepad_button_type_t::LEFT_THUMB, gamepad->wButtons & XINPUT_GAMEPAD_LEFT_THUMB);
        set_gamepad_button_state(&raw_input, gamepad_button_type_t::RIGHT_THUMB, gamepad->wButtons & XINPUT_GAMEPAD_RIGHT_THUMB);
        set_gamepad_button_state(&raw_input, gamepad_button_type_t::LEFT_SHOULDER, gamepad->wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER);
        set_gamepad_button_state(&raw_input, gamepad_button_type_t::RIGHT_SHOULDER, gamepad->wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER);
        set_gamepad_button_state(&raw_input, gamepad_button_type_t::CONTROLLER_A, gamepad->wButtons & XINPUT_GAMEPAD_A);
        set_gamepad_button_state(&raw_input, gamepad_button_type_t::CONTROLLER_B, gamepad->wButtons & XINPUT_GAMEPAD_B);
        set_gamepad_button_state(&raw_input, gamepad_button_type_t::CONTROLLER_X, gamepad->wButtons & XINPUT_GAMEPAD_X);
        set_gamepad_button_state(&raw_input, gamepad_button_type_t::CONTROLLER_Y, gamepad->wButtons & XINPUT_GAMEPAD_Y);
        
        set_gamepad_button_state(&raw_input, gamepad_button_type_t::LEFT_TRIGGER, gamepad->bLeftTrigger > 0, ((float32_t)gamepad->bLeftTrigger) / 256.0f);
        set_gamepad_button_state(&raw_input, gamepad_button_type_t::RIGHT_TRIGGER, gamepad->bRightTrigger > 0, ((float32_t)gamepad->bRightTrigger) / 256.0f);

        // 32767

        #define INPUT_DEADZONE 10000

        {
            float lx = gamepad->sThumbLX;
            float ly = gamepad->sThumbLY;

            //determine how far the controller is pushed
            float magnitude = sqrt(lx * lx + ly * ly);

            //determine the direction the controller is pushed
            float normalized_lx = lx / magnitude;
            float normalized_ly = ly / magnitude;

            float normalized_magnitude = 0;

            if (magnitude > INPUT_DEADZONE)
            {
                if (magnitude > 32767) magnitude = 32767;
  
                magnitude -= INPUT_DEADZONE;
                normalized_magnitude = magnitude / (32767 - INPUT_DEADZONE);
            }
            else
            {
                magnitude = 0.0;
                normalized_magnitude = 0.0;
            }

            magnitude /= 100.0f;

            lx /= 1000.0f;
            ly /= 1000.0f;
            
            set_gamepad_button_state(&raw_input, gamepad_button_type_t::LTHUMB_MOVE_RIGHT, magnitude > 0 && lx > 0, ((float32_t)lx));
            set_gamepad_button_state(&raw_input, gamepad_button_type_t::LTHUMB_MOVE_LEFT, magnitude > 0 && lx < 0, ((float32_t)lx));
            set_gamepad_button_state(&raw_input, gamepad_button_type_t::LTHUMB_MOVE_UP, magnitude > 0 && ly > 0, ((float32_t)ly));
            set_gamepad_button_state(&raw_input, gamepad_button_type_t::LTHUMB_MOVE_DOWN, magnitude > 0 && ly < 0, ((float32_t)ly));
        }


        {
            float lx = gamepad->sThumbRX;
            float ly = gamepad->sThumbRY;

            //determine how far the controller is pushed
            float magnitude = sqrt(lx * lx + ly * ly);

            //determine the direction the controller is pushed
            float normalized_lx = lx / magnitude;
            float normalized_ly = ly / magnitude;

            float normalized_magnitude = 0;

            if (magnitude > INPUT_DEADZONE)
            {
                if (magnitude > 32767) magnitude = 32767;
  
                magnitude -= INPUT_DEADZONE;
                normalized_magnitude = magnitude / (32767 - INPUT_DEADZONE);
            }
            else
            {
                magnitude = 0.0;
                normalized_magnitude = 0.0;
            }

            magnitude /= 100.0f;

            lx /= 1000.0f;
            ly /= -1000.0f;

            set_gamepad_button_state(&raw_input, gamepad_button_type_t::RTHUMB_MOVE_RIGHT, magnitude > 0 && lx > 0, ((float32_t)lx));
            set_gamepad_button_state(&raw_input, gamepad_button_type_t::RTHUMB_MOVE_LEFT, magnitude > 0 && lx < 0, ((float32_t)lx));
            set_gamepad_button_state(&raw_input, gamepad_button_type_t::RTHUMB_MOVE_UP, magnitude > 0 && ly > 0, ((float32_t)ly));
            set_gamepad_button_state(&raw_input, gamepad_button_type_t::RTHUMB_MOVE_DOWN, magnitude > 0 && ly < 0, ((float32_t)ly));
        }

        XINPUT_VIBRATION vibration;
        ZeroMemory( &vibration, sizeof(XINPUT_VIBRATION) );
        vibration.wLeftMotorSpeed = 0; // use any value between 0-65535 here
        vibration.wRightMotorSpeed = 0; // use any value between 0-65535 here
        XInputSetState( active_controller, &vibration );


        
        /*float32_t div = 1000.0f;
        set_gamepad_button_state(&raw_input, gamepad_button_type_t::LTHUMB_MOVE_RIGHT, gamepad->sThumbLX > 0, ((float32_t)gamepad->sThumbLX) / div);
        set_gamepad_button_state(&raw_input, gamepad_button_type_t::LTHUMB_MOVE_LEFT, gamepad->sThumbLX < 0, ((float32_t)gamepad->sThumbLX) / div);
        set_gamepad_button_state(&raw_input, gamepad_button_type_t::LTHUMB_MOVE_UP, gamepad->sThumbLY > 0, ((float32_t)gamepad->sThumbLY) / div);
        set_gamepad_button_state(&raw_input, gamepad_button_type_t::LTHUMB_MOVE_DOWN, gamepad->sThumbLY < 0, ((float32_t)gamepad->sThumbLY) / div);

        set_gamepad_button_state(&raw_input, gamepad_button_type_t::RTHUMB_MOVE_RIGHT, gamepad->sThumbRX > 0, ((float32_t)gamepad->sThumbRX) / div);
        set_gamepad_button_state(&raw_input, gamepad_button_type_t::RTHUMB_MOVE_LEFT, gamepad->sThumbRX < 0, ((float32_t)gamepad->sThumbRX) / div);
        set_gamepad_button_state(&raw_input, gamepad_button_type_t::RTHUMB_MOVE_UP, gamepad->sThumbRY > 0, ((float32_t)gamepad->sThumbRY) / div);
        set_gamepad_button_state(&raw_input, gamepad_button_type_t::RTHUMB_MOVE_DOWN, gamepad->sThumbRY < 0, ((float32_t)gamepad->sThumbRY) / div);*/
    }
}


static void parse_command_line_args(LPSTR cmdline, application_type_t *app_type, application_mode_t *app_mode, const char **application_name)
{
    uint32_t parameter_start = 0;
    bool parsing_parameter = 1;

    char parameter[3];

    uint32_t i = 0;
    for (char *c = cmdline; *c; ++c)
    {
        if (parsing_parameter && i < 2)
        {
            parameter[i++] = *c;
        }
        
        /*if (*c == ' ')
        {
            parsing_parameter = 1;
            }*/
    }

    parameter[2] = 0;

    if (strcmp("sv", parameter) == 0)
    {
        *app_type = application_type_t::WINDOW_APPLICATION_MODE;
        *app_mode = application_mode_t::SERVER_MODE;

        *application_name = "Server";
    }
    else if (strcmp("cl", parameter) == 0)
    {
        *app_type = application_type_t::WINDOW_APPLICATION_MODE;
        *app_mode = application_mode_t::CLIENT_MODE;

        *application_name = "Saska";
    }
}


static float32_t measure_time_difference(LARGE_INTEGER begin_time, LARGE_INTEGER end_time, LARGE_INTEGER frequency)
{
    return float32_t(end_time.QuadPart - begin_time.QuadPart) / float32_t(frequency.QuadPart);
}


static void init_free_list_allocator_head(free_list_allocator_t *allocator)
{
    allocator->free_block_head = (free_block_header_t *)allocator->start;
    allocator->free_block_head->free_block_size = allocator->available_bytes;
}



// Public
void request_quit(void)
{
    running = 0;
    PostQuitMessage(0);
}


raw_input_t *get_raw_input(void)
{
    return &raw_input;
}


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


void enable_cursor_display(void)
{
    raw_input.show_cursor = 1;
    ShowCursor(1);
}


void disable_cursor_display(void)
{
    raw_input.show_cursor = 0;
    ShowCursor(0);
}
