/* ------------------------------------------------------------   

     TODO:
     * Get basic UI working (rendering quads, making sure resizing of window doens't completely distort it, etc...)
        - Create the render pass for rendering UI /
        - Create VBO for rendering UI boxes /
        - Create shader for rendering UI boxes (maybe just use the font rendering one for now) - delete code for fonts, and just use it for rendering ui boxes /
        - Create CPU side buffer for all the vertices that will get sent to the GPU /
        - Start writing update_ui() function
            - Start with updating GPU side buffer with the vertices with vkCmdUpdateBuffer (secondary queue)
            - Render UI Boxes from there into secondary queue
            - COMMAND_BUFFER_BIND_VBOX()
        - Once finished with update_ui, make sure that scaling works
        - Create UI Box parent system
            - Make sure that the child position calculation doesn't mess up because of aspect ratio changes... !!!
        - Find way to integrate Font rendering (possibly bound to UI Boxes? - even invisible ones)
            - Create font texture + font file. 
            - Integrate the texture into the game
            - Simply sample from the texture (to check for how the uvs work)
            - Create font interface system, to create all the uvs, positions, etc...
     * Get font rendering working, displaying textured images (quads) on the screen
     * Get started working on the in-game console to make gameplay development quicker and easier
     * Start working on debugging post processing system (VML)
        - At start of frame capture: 
            - Blit screen into texture and display it in "freeze-mode"
            - Create way to bind samplers to the frame capture (in setup code)
            - Blit the bound textures into the sampler2d_t structs which contain the linearly formatted image
        - Get pointed coordinate with mouse (make sure the math is correct)
        - Integrate VML:
            - Find a way to bind samplers to their respective images (memory)
            - Find a way to bind push_constant to some memory
     * Start getting the gameplay moving (add better physics, sliding through different terrains, etc...)
        - Add terrain noise
        - Change terrain morphing point to triangle
        - Add physics
     * Add skeletal animation (loading models + animations, etc...)
     * Refactor code: separate the stuff a bit better

  ------------------------------------------------------------ */

#define GLFW_INCLUDE_VULKAN

#include <GLFW/glfw3.h>
#include <iostream>
#include <stb_image.h>

#if defined(UNITY_BUILD)
#include "memory.cpp"
#include "ui.cpp"
#include "game.cpp"
#include "world.cpp"
#include "script.cpp"
#include "graphics.cpp"
#include "vulkan.cpp"

#else
#include <vulkan/vulkan.h>

#include "memory.hpp"

#include "vulkan.hpp"
#include "core.hpp"
#include "world.hpp"
#include <stdlib.h>

#include "game.hpp"
#include "vulkan.hpp"
#endif

#define DEBUG_FILE ".debug"

debug_output_t output_file;

global_var bool g_running;
global_var double g_game_time = 0.0f;
global_var double g_dt = 0.0f;
global_var GLFWwindow *g_window = nullptr;

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
output_debug(const char *format
	     , ...)
{
    va_list arg_list;
    
    va_start(arg_list, format);

    fprintf(output_file.fp
	    , format
	    , arg_list);

    va_end(arg_list);

    fflush(output_file.fp);
}

float32_t get_dt(void)
{
    return(g_dt);
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

// window procs:
#define MAX_KEYS 350
#define MAX_MB 5

internal_function void glfw_window_resize_proc(GLFWwindow *win, int32_t w, int32_t h)
{
    input_state_t *input_state = (input_state_t *)glfwGetWindowUserPointer(win);
    input_state->window_width = w;
    input_state->window_height = h;
    input_state->resized = true;
}

internal_function void set_key_state(input_state_t *input_state, keyboard_button_type_t button, int32_t action)
{
    if (action == GLFW_PRESS)
    {
        keyboard_button_input_t *key = &input_state->keyboard[button];
        key->is_down = 1;
        key->down_amount += g_dt;
    }
    else if (action == GLFW_RELEASE)
    {
        keyboard_button_input_t *key = &input_state->keyboard[button];
        key->is_down = 0;
        key->down_amount = 0.0f;
    }
}

internal_function void glfw_keyboard_input_proc(GLFWwindow *win, int32_t key, int32_t scancode, int32_t action, int32_t mods)
{
    input_state_t *input_state = (input_state_t *)glfwGetWindowUserPointer(win);
    switch(key)
    {
    case GLFW_KEY_A: { set_key_state(input_state, keyboard_button_type_t::A, action); } break;
    case GLFW_KEY_B: { set_key_state(input_state, keyboard_button_type_t::B, action); } break;
    case GLFW_KEY_C: { set_key_state(input_state, keyboard_button_type_t::C, action); } break;
    case GLFW_KEY_D: { set_key_state(input_state, keyboard_button_type_t::D, action); } break;
    case GLFW_KEY_E: { set_key_state(input_state, keyboard_button_type_t::E, action); } break;
    case GLFW_KEY_F: { set_key_state(input_state, keyboard_button_type_t::F, action); } break;
    case GLFW_KEY_G: { set_key_state(input_state, keyboard_button_type_t::G, action); } break;
    case GLFW_KEY_H: { set_key_state(input_state, keyboard_button_type_t::H, action); } break;
    case GLFW_KEY_I: { set_key_state(input_state, keyboard_button_type_t::I, action); } break;
    case GLFW_KEY_J: { set_key_state(input_state, keyboard_button_type_t::J, action); } break;
    case GLFW_KEY_K: { set_key_state(input_state, keyboard_button_type_t::K, action); } break;
    case GLFW_KEY_L: { set_key_state(input_state, keyboard_button_type_t::L, action); } break;
    case GLFW_KEY_M: { set_key_state(input_state, keyboard_button_type_t::M, action); } break;
    case GLFW_KEY_N: { set_key_state(input_state, keyboard_button_type_t::N, action); } break;
    case GLFW_KEY_O: { set_key_state(input_state, keyboard_button_type_t::O, action); } break;
    case GLFW_KEY_P: { set_key_state(input_state, keyboard_button_type_t::P, action); } break;
    case GLFW_KEY_Q: { set_key_state(input_state, keyboard_button_type_t::Q, action); } break;
    case GLFW_KEY_R: { set_key_state(input_state, keyboard_button_type_t::R, action); } break;
    case GLFW_KEY_S: { set_key_state(input_state, keyboard_button_type_t::S, action); } break;
    case GLFW_KEY_T: { set_key_state(input_state, keyboard_button_type_t::T, action); } break;
    case GLFW_KEY_U: { set_key_state(input_state, keyboard_button_type_t::U, action); } break;
    case GLFW_KEY_V: { set_key_state(input_state, keyboard_button_type_t::V, action); } break;
    case GLFW_KEY_W: { set_key_state(input_state, keyboard_button_type_t::W, action); } break;
    case GLFW_KEY_X: { set_key_state(input_state, keyboard_button_type_t::X, action); } break;
    case GLFW_KEY_Y: { set_key_state(input_state, keyboard_button_type_t::Y, action); } break;
    case GLFW_KEY_Z: { set_key_state(input_state, keyboard_button_type_t::Z, action); } break;
    case GLFW_KEY_0: { set_key_state(input_state, keyboard_button_type_t::ZERO, action); } break;
    case GLFW_KEY_1: { set_key_state(input_state, keyboard_button_type_t::ONE, action); } break;
    case GLFW_KEY_2: { set_key_state(input_state, keyboard_button_type_t::TWO, action); } break;
    case GLFW_KEY_3: { set_key_state(input_state, keyboard_button_type_t::THREE, action); } break;
    case GLFW_KEY_4: { set_key_state(input_state, keyboard_button_type_t::FOUR, action); } break;
    case GLFW_KEY_5: { set_key_state(input_state, keyboard_button_type_t::FIVE, action); } break;
    case GLFW_KEY_6: { set_key_state(input_state, keyboard_button_type_t::SIX, action); } break;
    case GLFW_KEY_7: { set_key_state(input_state, keyboard_button_type_t::SEVEN, action); } break;
    case GLFW_KEY_8: { set_key_state(input_state, keyboard_button_type_t::EIGHT, action); } break;
    case GLFW_KEY_9: { set_key_state(input_state, keyboard_button_type_t::NINE, action); } break;
    case GLFW_KEY_UP: { set_key_state(input_state, keyboard_button_type_t::UP, action); } break;
    case GLFW_KEY_LEFT: { set_key_state(input_state, keyboard_button_type_t::LEFT, action); } break;
    case GLFW_KEY_DOWN: { set_key_state(input_state, keyboard_button_type_t::DOWN, action); } break;
    case GLFW_KEY_RIGHT: { set_key_state(input_state, keyboard_button_type_t::RIGHT, action); } break;
    case GLFW_KEY_SPACE: { set_key_state(input_state, keyboard_button_type_t::SPACE, action); } break;
    case GLFW_KEY_LEFT_SHIFT: { set_key_state(input_state, keyboard_button_type_t::LEFT_SHIFT, action); } break;
    case GLFW_KEY_LEFT_CONTROL: { set_key_state(input_state, keyboard_button_type_t::LEFT_CONTROL, action); } break;
    case GLFW_KEY_ENTER: { set_key_state(input_state, keyboard_button_type_t::ENTER, action); } break;
    case GLFW_KEY_BACKSPACE: { set_key_state(input_state, keyboard_button_type_t::BACKSPACE, action); } break;
    case GLFW_KEY_ESCAPE: { set_key_state(input_state, keyboard_button_type_t::ESCAPE, action); } break;
    }
}

internal_function void
glfw_mouse_position_proc(GLFWwindow *win, float64_t x, float64_t y)
{
    input_state_t *input_state = (input_state_t *)glfwGetWindowUserPointer(win);

    input_state->previous_cursor_pos_x = input_state->cursor_pos_x;
    input_state->previous_cursor_pos_y = input_state->cursor_pos_y;
    input_state->cursor_pos_x = x;
    input_state->cursor_pos_y = y;
    input_state->cursor_moved = true;
}

internal_function void set_mouse_button_state(input_state_t *input_state, mouse_button_type_t button, int32_t action)
{
    if (action == GLFW_PRESS)
    {
        mouse_button_input_t *mouse_button = &input_state->mouse_buttons[button];
        mouse_button->is_down = 1;
        mouse_button->down_amount += g_dt;
    }
    else if (action == GLFW_RELEASE)
    {
        mouse_button_input_t *mouse_button = &input_state->mouse_buttons[button];
        mouse_button->is_down = 0;
        mouse_button->down_amount = 0.0f;
    }
}

internal_function void
glfw_mouse_button_proc(GLFWwindow *win, int32_t button, int32_t action, int32_t mods)
{
    input_state_t *input_state = (input_state_t *)glfwGetWindowUserPointer(win);
    switch(button)
    {
    case GLFW_MOUSE_BUTTON_LEFT: { set_mouse_button_state(input_state, mouse_button_type_t::MOUSE_LEFT, action); } break;
    case GLFW_MOUSE_BUTTON_RIGHT: { set_mouse_button_state(input_state, mouse_button_type_t::MOUSE_RIGHT, action); } break;
    case GLFW_MOUSE_BUTTON_MIDDLE: { set_mouse_button_state(input_state, mouse_button_type_t::MOUSE_MIDDLE, action); } break;
    }
}

internal_function void
glfw_char_input_proc(GLFWwindow *win, uint32_t code_point)
{
    input_state_t *input_state = (input_state_t *)glfwGetWindowUserPointer(win);
    if (input_state->char_count != MAX_CHARS)
    {
        input_state->char_stack[input_state->char_count++] = (char)code_point;
    }
}

#include <chrono>

internal_function void
init_free_list_allocator_head(free_list_allocator_t *allocator = &free_list_allocator_global)
{
    allocator->free_block_head = (free_block_header_t *)allocator->start;
    allocator->free_block_head->free_block_size = allocator->available_bytes;
}

internal_function bool32_t create_vulkan_surface_proc(VkInstance *instance, VkSurfaceKHR *dst_surface, void *window_data)
{
    GLFWwindow **window = (GLFWwindow **)(window_data);
    return glfwCreateWindowSurface(*instance, *window, nullptr, dst_surface);
}

struct create_vulkan_surface_agnostic : create_vulkan_surface
{
    GLFWwindow *window_data;
    bool32_t create_proc(void) override
    {
        return glfwCreateWindowSurface(*instance, window_data, nullptr, surface);   
    }
};

void enable_cursor_display(void)
{
    glfwSetInputMode(g_window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
}

void disable_cursor_display(void)
{
    glfwSetInputMode(g_window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
}

int32_t main(int32_t argc, char * argv[])
{    
    open_debug_file();
	
    OUTPUT_DEBUG_LOG("%s\n", "starting session");

    linear_allocator_global.capacity = megabytes(30);
    linear_allocator_global.start = linear_allocator_global.current = malloc(linear_allocator_global.capacity);
	
    stack_allocator_global.capacity = megabytes(10);
    stack_allocator_global.start = stack_allocator_global.current = malloc(stack_allocator_global.capacity);

    free_list_allocator_global.available_bytes = megabytes(50);
    free_list_allocator_global.start = malloc(free_list_allocator_global.available_bytes);
    init_free_list_allocator_head(&free_list_allocator_global);
	
    OUTPUT_DEBUG_LOG("stack allocator start address : %p\n", stack_allocator_global.current);
    
    if (!glfwInit())
    {
        return(-1);
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    input_state_t input_state = {};
    input_state.window_width = 1700;
    input_state.window_height = 800;

    g_window = glfwCreateWindow(input_state.window_width, input_state.window_height, "Game", NULL, NULL);

    if (!g_window)
    {
        glfwTerminate();
        return(-1);
    }

    glfwSetInputMode(g_window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    glfwSetWindowUserPointer(g_window, &input_state);
    glfwSetKeyCallback(g_window, glfw_keyboard_input_proc);
    glfwSetMouseButtonCallback(g_window, glfw_mouse_button_proc);
    glfwSetCursorPosCallback(g_window, glfw_mouse_position_proc);
    glfwSetWindowSizeCallback(g_window, glfw_window_resize_proc);
    glfwSetCharCallback(g_window, glfw_char_input_proc);

    create_vulkan_surface_agnostic create_surface_agnostic_platform = {};
    
    create_surface_agnostic_platform.window_data = g_window;
    
    initialize_graphics_api(&create_surface_agnostic_platform, &input_state);
    initialize_game(&input_state);

    float32_t fps = 0.0f;

    {
        float64_t m_x, m_y;
        glfwGetCursorPos(g_window, &m_x, &m_y);
        input_state.cursor_pos_x = (float32_t)m_x;
        input_state.cursor_pos_y = (float32_t)m_y;
    }
    
    auto now = std::chrono::high_resolution_clock::now();
    while(!glfwWindowShouldClose(g_window))
    {
        glfwPollEvents();
        update_game(&input_state, g_dt);	   

        clear_linear();

        input_state.cursor_moved = false;

        input_state.char_count = 0;
        input_state.keyboard[keyboard_button_type_t::BACKSPACE].is_down = false;
        input_state.keyboard[keyboard_button_type_t::ENTER].is_down = false;
        input_state.keyboard[keyboard_button_type_t::ESCAPE].is_down = false;
        input_state.keyboard[keyboard_button_type_t::LEFT_CONTROL].is_down = false;

        double xpos, ypos;
        glfwGetCursorPos(g_window, &xpos, &ypos);
        input_state.normalized_cursor_position = vector2_t((float32_t)xpos, (float32_t)ypos);
        
        auto new_now = std::chrono::high_resolution_clock::now();
        g_dt = std::chrono::duration<double, std::chrono::seconds::period>(new_now - now).count();
        g_game_time += g_dt;
        input_state.dt = g_dt;

        now = new_now;
    }

    OUTPUT_DEBUG_LOG("stack allocator start address is : %p\n", stack_allocator_global.current);
    OUTPUT_DEBUG_LOG("stack allocator allocated %d bytes\n", (uint32_t)((u8 *)stack_allocator_global.current - (u8 *)stack_allocator_global.start));
	
    OUTPUT_DEBUG_LOG("finished session : FPS : %f\n", fps);

    //	printf("%f\n", fps);

    std::cout << std::endl;
	
    close_debug_file();

    destroy_swapchain();
    destroy_game();
	
    destroy_vulkan_state();
	
    glfwDestroyWindow(g_window);
    glfwTerminate();

    std::cout << "Finished session" << std::endl;

    return(0);
}
