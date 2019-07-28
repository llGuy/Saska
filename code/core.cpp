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
window_data_t window;

window_data_t *
get_window_data(void)
{
    return(&window);
}

internal bool running;

file_contents_t
read_file(const char *filename
	  , const char *flags
	  , linear_allocator_t *allocator)
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

internal void
open_debug_file(void)
{
    output_file.fp = fopen(DEBUG_FILE, "w+");
    assert(output_file.fp >= NULL);
}

internal void
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

internal void
glfw_window_resize_proc(GLFWwindow *win, int32_t w, int32_t h)
{
    window_data_t *w_data = (window_data_t *)glfwGetWindowUserPointer(win);
    w_data->w = w;
    w_data->h = h;
    w_data->window_resized = true;
}

internal void
glfw_keyboard_input_proc(GLFWwindow *win, int32_t key, int32_t scancode, int32_t action, int32_t mods)
{
    if (key < MAX_KEYS)
    {
	window_data_t *w_data = (window_data_t *)glfwGetWindowUserPointer(win);
	if (action == GLFW_PRESS) w_data->key_map[key] = true;
	else if (action == GLFW_RELEASE) w_data->key_map[key] = false;
    }
}

internal void
glfw_mouse_position_proc(GLFWwindow *win, float64_t x, float64_t y)
{
    window_data_t *w_data = (window_data_t *)glfwGetWindowUserPointer(win);

    w_data->prev_m_x = w_data->m_x;
    w_data->prev_m_y = w_data->m_y;
    
    w_data->m_x = x;
    w_data->m_y = y;
    w_data->m_moved = true;
}

internal void
glfw_mouse_button_proc(GLFWwindow *win, int32_t button, int32_t action, int32_t mods)
{
    if (button < MAX_MB)
    {
	window_data_t *w_data = (window_data_t *)glfwGetWindowUserPointer(win);
	if (action == GLFW_PRESS) w_data->mb_map[button] = true;
	else if (action == GLFW_RELEASE) w_data->mb_map[button] = false;
    }
}

internal void
glfw_char_input_proc(GLFWwindow *win, uint32_t code_point)
{
    window_data_t *w_data = (window_data_t *)glfwGetWindowUserPointer(win);
    if (w_data->char_count != window_data_t::MAX_CHARS)
    {
        w_data->char_stack[w_data->char_count++] = (char)code_point;
    }
}

#include <chrono>

internal void
init_free_list_allocator_head(free_list_allocator_t *allocator = &free_list_allocator_global)
{
    allocator->free_block_head = (free_block_header_t *)allocator->start;
    allocator->free_block_head->free_block_size = allocator->available_bytes;
}

int32_t
main(int32_t argc
     , char * argv[])
{

    
    open_debug_file();
	
    OUTPUT_DEBUG_LOG("%s\n", "starting session");

    linear_allocator_global.capacity = megabytes(30);
    linear_allocator_global.start = linear_allocator_global.current = malloc(linear_allocator_global.capacity);
	
    stack_allocator_global.capacity = megabytes(10);
    stack_allocator_global.start = stack_allocator_global.current = malloc(stack_allocator_global.capacity);

    free_list_allocator_global.available_bytes = megabytes(10);
    free_list_allocator_global.start = malloc(free_list_allocator_global.available_bytes);
    init_free_list_allocator_head(&free_list_allocator_global);
	
    OUTPUT_DEBUG_LOG("stack allocator start address : %p\n", stack_allocator_global.current);
    
    if (!glfwInit())
    {
        return(-1);
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    allocate_memory_buffer(window.key_map, MAX_KEYS);
    allocate_memory_buffer(window.mb_map, MAX_MB);
	
    window.w = 1700;
    window.h = 800;
	
    window.window = glfwCreateWindow(window.w
                                     , window.h
                                     , "Game"
                                     , NULL
                                     , NULL);

    if (!window.window)
    {
        glfwTerminate();
        return(-1);
    }

    glfwSetInputMode(window.window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    glfwSetWindowUserPointer(window.window, &window);
    glfwSetKeyCallback(window.window, glfw_keyboard_input_proc);
    glfwSetMouseButtonCallback(window.window, glfw_mouse_button_proc);
    glfwSetCursorPosCallback(window.window, glfw_mouse_position_proc);
    glfwSetWindowSizeCallback(window.window, glfw_window_resize_proc);
    glfwSetCharCallback(window.window, glfw_char_input_proc);

    vulkan_state_t vk = {};

    init_vulkan_state(&vk, window.window);


    make_game(&vk
              , &vk.gpu
              , &vk.swapchain
              , &window);
	
	

    float32_t fps = 0.0f;

    {
        float64_t m_x, m_y;
        glfwGetCursorPos(window.window, &m_x, &m_y);
        window.m_x = (float32_t)m_x;
        window.m_y = (float32_t)m_y;
    }
        
    auto now = std::chrono::high_resolution_clock::now();
    while(!glfwWindowShouldClose(window.window))
    {
        glfwPollEvents();
        update_game(&vk.gpu, &vk.swapchain, &window, &vk, window.dt);	   

        clear_linear();

        window.m_moved = false;

        window.char_count = 0;
        window.key_map[GLFW_KEY_BACKSPACE] = false;
        window.key_map[GLFW_KEY_ENTER] = false;
        window.key_map[GLFW_KEY_ESCAPE] = false;
        window.key_map[GLFW_KEY_LEFT_CONTROL] = false;

        double xpos, ypos;
        glfwGetCursorPos(window.window, &xpos, &ypos);
        window.normalized_cursor_position = vector2_t((float32_t)xpos, (float32_t)ypos);
        
        auto new_now = std::chrono::high_resolution_clock::now();
        window.dt = std::chrono::duration<float32_t, std::chrono::seconds::period>(new_now - now).count();

        now = new_now;
    }

    OUTPUT_DEBUG_LOG("stack allocator start address is : %p\n", stack_allocator_global.current);
    OUTPUT_DEBUG_LOG("stack allocator allocated %d bytes\n", (uint32_t)((u8 *)stack_allocator_global.current - (u8 *)stack_allocator_global.start));
	
    OUTPUT_DEBUG_LOG("finished session : FPS : %f\n", fps);

    //	printf("%f\n", fps);

    std::cout << std::endl;
	
    close_debug_file();

    // destroy rnd and vk
    vkDeviceWaitIdle(vk.gpu.logical_device);
    destroy_swapchain(&vk);
    destroy_game(&vk.gpu);
	
    destroy_vulkan_state(&vk);
	
    glfwDestroyWindow(window.window);
    glfwTerminate();

    std::cout << "Finished session" << std::endl;

    return(0);
}
