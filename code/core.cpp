#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

#include "vulkan.hpp"
#include "core.hpp"
#include "world.hpp"
#include <stdlib.h>

#include "game.hpp"
#include "vulkan.hpp"

#define DEBUG_FILE ".debug"


Linear_Allocator linear_allocator_global;
Stack_Allocator stack_allocator_global;
Free_List_Allocator free_list_allocator_global;

Debug_Output output_file;
Window_Data window;

internal bool running;

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

void *
allocate_linear(u32 alloc_size
		, Alignment alignment
		, const char *name
		, Linear_Allocator *allocator)
{
    void *prev = allocator->current;
    void *new_crnt = (byte *)allocator->current + alloc_size;

    allocator->current = new_crnt;

    return(prev);
}

void
clear_linear(Linear_Allocator *allocator)
{
    allocator->current = allocator->start;
}

void *
allocate_stack(u32 allocation_size
	       , Alignment alignment
	       , const char *name
	       , Stack_Allocator *allocator)
{
    byte *would_be_address;

    if (allocator->allocation_count == 0) would_be_address = (u8 *)allocator->current + sizeof(Stack_Allocation_Header);
    else would_be_address = (u8 *)allocator->current
	     + sizeof(Stack_Allocation_Header) * 2 
	     + ((Stack_Allocation_Header *)(allocator->current))->size;
    
    // calculate the aligned address that is needed
    u8 alignment_adjustment = get_alignment_adjust(would_be_address
						   , alignment);

    // start address of (header + allocation)
    byte *start_address = (would_be_address + alignment_adjustment) - sizeof(Stack_Allocation_Header);
    assert((start_address + sizeof(Stack_Allocation_Header) + allocation_size) < (u8 *)allocator->start + allocator->capacity);

    Stack_Allocation_Header *header = (Stack_Allocation_Header *)start_address;
    
#if DEBUG
    header->allocation_name = name;
    OUTPUT_DEBUG_LOG("stack allocation for \"%s\"\n", name);
#endif

    header->size = allocation_size;
    header->prev = allocator->allocation_count == 0 ? nullptr : (Stack_Allocation_Header *)allocator->current;

    allocator->current = (void *)header;
    ++(allocator->allocation_count);

    return(start_address + sizeof(Stack_Allocation_Header));
}

void
extend_stack_top(u32 extension_size
		 , Stack_Allocator *allocator)
{
    Stack_Allocation_Header *current_header = (Stack_Allocation_Header *)allocator->current;
    current_header->size += extension_size;
}

void
pop_stack(Stack_Allocator *allocator)
{
    Stack_Allocation_Header *current_header = (Stack_Allocation_Header *)allocator->current;
    
#if DEBUG
    if (allocator->allocation_count == 1)
    {
	OUTPUT_DEBUG_LOG("cleared stack : last allocation was \"%s\"\n", current_header->allocation_name);
    }
    else if (allocator->allocation_count == 0)
    {
	OUTPUT_DEBUG_LOG("%s\n", "stack already cleared : pop stack call ignored");
    }
    else
    {
	OUTPUT_DEBUG_LOG("poping allocation \"%s\" -> new head is \"%s\"\n", current_header->allocation_name
			 , ((Stack_Allocation_Header *)(current_header->prev))->allocation_name);
    }
#endif

    if (allocator->allocation_count == 1) allocator->current = allocator->start;
    else allocator->current = current_header->prev;
    --(allocator->allocation_count);
}

internal void
init_free_list_allocator_head(Free_List_Allocator *allocator = &free_list_allocator_global)
{
    allocator->free_block_head = (Free_Block_Header *)allocator->start;
    allocator->free_block_head->free_block_size = allocator->available_bytes;
}

void *
allocate_free_list(u32 allocation_size
		   , Alignment alignment
		   , const char *name
		   , Free_List_Allocator *allocator)
{
    u32 total_allocation_size = allocation_size + sizeof(Free_List_Allocation_Header);
    // find best fit free block
    // TODO(luc) : make free list allocator adjust the smallest free block according to the alignment as well
    Free_Block_Header *previous_free_block = nullptr;
    Free_Block_Header *smallest_free_block = allocator->free_block_head;
    for (Free_Block_Header *header = allocator->free_block_head
	     ; header
	     ; header = header->next_free_block)
    {
	if (header->free_block_size >= total_allocation_size)
	{
	    if (smallest_free_block->free_block_size >= header->free_block_size)
	    {
		smallest_free_block = header;
		break;
	    }
	}
	previous_free_block = header;
    }
    Free_Block_Header *next = smallest_free_block->next_free_block;
    if (previous_free_block)
    {
	u32 previous_smallest_block_size = smallest_free_block->free_block_size;
	previous_free_block->next_free_block = (Free_Block_Header *)(((byte *)smallest_free_block) + total_allocation_size);
	previous_free_block->next_free_block->free_block_size = previous_smallest_block_size - total_allocation_size;
	previous_free_block->next_free_block->next_free_block = next;
    }
    else
    {
	Free_Block_Header *new_block = (Free_Block_Header *)(((byte *)smallest_free_block) + total_allocation_size);
	allocator->free_block_head = new_block;
	new_block->free_block_size = smallest_free_block->free_block_size - total_allocation_size;
	new_block->next_free_block = smallest_free_block->next_free_block;
    }
    
    Free_List_Allocation_Header *header = (Free_List_Allocation_Header *)smallest_free_block;
    header->size = allocation_size;
#if DEBUG
    header->name = name;
#endif
    return((byte *)header + sizeof(Free_List_Allocation_Header));
}

// TODO(luc) : optimize free list allocator so that all the blocks are stored in order
void
deallocate_free_list(void *pointer
		     , Free_List_Allocator *allocator)
{
    Free_List_Allocation_Header *allocation_header = (Free_List_Allocation_Header *)((byte *)pointer - sizeof(Free_List_Allocation_Header));
    Free_Block_Header *new_free_block_header = (Free_Block_Header *)allocation_header;
    new_free_block_header->free_block_size = allocation_header->size + sizeof(Free_Block_Header);

    Free_Block_Header *previous = nullptr;
    Free_Block_Header *current_header = allocator->free_block_head;

    Free_Block_Header *viable_prev = nullptr;
    Free_Block_Header *viable_next = nullptr;
    
    // check if possible to merge free blocks
    bool merged = false;
    for (; current_header
	     ; current_header = current_header->next_free_block)
    {
	if (new_free_block_header > previous && new_free_block_header < current_header)
	{
	    viable_prev = previous;
	    viable_next = current_header;
	}
	
	// check if free blocks will overlap
	// does the header go over the newly freed header
	if (current_header->free_block_size + (byte *)current_header >= (byte *)new_free_block_header
	    && (byte *)current_header < (byte *)new_free_block_header)
	{
	    current_header->free_block_size = ((byte *)new_free_block_header + new_free_block_header->free_block_size) - (byte *)current_header;
	    new_free_block_header = current_header;
	    previous = current_header;
	    merged = true;
	    continue;
	}
	// does the newly freed header go over the header
	if ((byte *)current_header <= (byte *)new_free_block_header + new_free_block_header->free_block_size
	    && (byte *)current_header > (byte *)new_free_block_header)
	{
	    // if current header is not the head of the list
	    new_free_block_header->free_block_size = (byte *)((byte *)current_header + current_header->free_block_size) - (byte *)new_free_block_header; 
	    /*if (previous)
	      {
	      previous->next_free_block = new_free_block_header;
	      }*/
	    if (!previous)
	    {
		allocator->free_block_head = new_free_block_header;
	    }
	    new_free_block_header->next_free_block = current_header->next_free_block;
	    merged = true;
	    
	    previous = current_header;
	    continue;
	}

	if (merged) return;
	
	previous = current_header;
    }

    if (merged) return;

    // put the blocks in order if no blocks merged
    // if viable_prev == nullptr && viable_next != nullptr -> new block should be before the head
    // if viable_prev != nullptr && viable_next != nullptr -> new block should be between prev and next
    // if viable_prev == nullptr && viable_next == nullptr -> new block should be after the last header
    if (!viable_prev && viable_next)
    {
	Free_Block_Header *old_head = allocator->free_block_head;
	allocator->free_block_head = new_free_block_header;
	allocator->free_block_head->next_free_block = old_head;
    }
    else if (viable_prev && viable_next)
    {
	viable_prev->next_free_block = new_free_block_header;
	new_free_block_header->next_free_block = viable_next;
    }
    else if (!viable_prev && !viable_next)
    {
	// at the end of the loop, previous is that last current before current = nullptr
	previous->next_free_block = new_free_block_header;
    }
}

struct Memory_Register_Info
{
    u32 active_count = 0;
    void *p; // pointer to the actual object
    u32 size;
};

global_var Free_List_Allocator object_allocator;
global_var Hash_Table_Inline<Memory_Register_Info /* destroyed count */, 20, 4, 4> objects_list("map.object_removed_list");

void
init_manager(void)
{
    object_allocator.start = malloc(megabytes(2));
    object_allocator.available_bytes = megabytes(2);
    memset(object_allocator.start, 0, object_allocator.available_bytes);
	
    object_allocator.free_block_head = (Free_Block_Header *)object_allocator.start;
    object_allocator.free_block_head->free_block_size = object_allocator.available_bytes;
}
    
Registered_Memory_Base
register_memory(const Constant_String &id
		, u32 bytes_size)
{
    void *p = allocate_free_list(bytes_size
				 , 1
				 , ""
				 , &object_allocator);

    Memory_Register_Info register_info {0, p, bytes_size};
    objects_list.insert(id.hash, register_info, "");

    return(Registered_Memory_Base(p, id, bytes_size));
}

Registered_Memory_Base
register_existing_memory(void *p
			 , const Constant_String &id
			 , u32 bytes_size)
{
    Memory_Register_Info register_info {0, p, bytes_size};
    objects_list.insert(id.hash, register_info, "");

    return(Registered_Memory_Base(p, id, bytes_size));
}

void
deregister_memory(const Constant_String &id)
{
    objects_list.remove(id.hash);
}

Registered_Memory_Base
get_memory(const Constant_String &id)
{
    Memory_Register_Info *info = objects_list.get(id.hash);

    if (!info)
    {
	OUTPUT_DEBUG_LOG("unable to find element %s\n", id.str);
    }
	
    return(Registered_Memory_Base(info->p, id, info->size));
}

void
remove_memory(const Constant_String &id)
{
    Memory_Register_Info *info = objects_list.get(id.hash);
    if (info->active_count == 0)
    {
	deallocate_free_list(info->p, &object_allocator);
    }
    else
    {
	// error, cannot delete object yet
    }
}

void
decrease_shared_count(const Constant_String &id)
{
    Memory_Register_Info *ri = objects_list.get(id.hash);
    --ri->active_count;
}

void
increase_shared_count(const Constant_String &id)
{
    Memory_Register_Info *ri = objects_list.get(id.hash);
    ++ri->active_count;
}

// window procs:
#define MAX_KEYS 350
#define MAX_MB 5

internal void
glfw_window_resize_proc(GLFWwindow *win, s32 w, s32 h)
{
    Window_Data *w_data = (Window_Data *)glfwGetWindowUserPointer(win);
    w_data->w = w;
    w_data->h = h;
    w_data->window_resized = true;
}

internal void
glfw_keyboard_input_proc(GLFWwindow *win, s32 key, s32 scancode, s32 action, s32 mods)
{
    if (key < MAX_KEYS)
    {
	Window_Data *w_data = (Window_Data *)glfwGetWindowUserPointer(win);
	if (action == GLFW_PRESS) w_data->key_map[key] = true;
	else if (action == GLFW_RELEASE) w_data->key_map[key] = false;
    }
}

internal void
glfw_mouse_position_proc(GLFWwindow *win, f64 x, f64 y)
{
    Window_Data *w_data = (Window_Data *)glfwGetWindowUserPointer(win);
    w_data->m_x = x;
    w_data->m_y = y;
    w_data->m_moved = true;
}

internal void
glfw_mouse_button_proc(GLFWwindow *win, s32 button, s32 action, s32 mods)
{
    if (button < MAX_MB)
    {
	Window_Data *w_data = (Window_Data *)glfwGetWindowUserPointer(win);
	if (action == GLFW_PRESS) w_data->mb_map[button] = true;
	else if (action == GLFW_RELEASE) w_data->mb_map[button] = false;
    }
}

#include <chrono>

s32
main(s32 argc
     , char * argv[])
{
    try
    {
	open_debug_file();
	
	OUTPUT_DEBUG_LOG("%s\n", "starting session");

	linear_allocator_global.capacity = megabytes(megabytes(4));
	linear_allocator_global.start = linear_allocator_global.current = malloc(linear_allocator_global.capacity);
	
	stack_allocator_global.capacity = megabytes(8);
	stack_allocator_global.start = stack_allocator_global.current = malloc(stack_allocator_global.capacity);

	free_list_allocator_global.available_bytes = megabytes(8);
	free_list_allocator_global.start = malloc(free_list_allocator_global.available_bytes);
	init_free_list_allocator_head(&free_list_allocator_global);
	
	OUTPUT_DEBUG_LOG("stack allocator start address : %p\n", stack_allocator_global.current);
    
	if (!glfwInit())
	{
	    return(0);
	}

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

	allocate_memory_buffer(window.key_map, MAX_KEYS);
	allocate_memory_buffer(window.mb_map, MAX_MB);
	
	window.w = 1280;
	window.h = 720;
	
	window.window = glfwCreateWindow(window.w
					 , window.h
					 , "Vulkan App"
					 , NULL
					 , NULL);

	glfwSetInputMode(window.window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
	glfwSetWindowUserPointer(window.window, &window);
	glfwSetKeyCallback(window.window, glfw_keyboard_input_proc);
	glfwSetMouseButtonCallback(window.window, glfw_mouse_button_proc);
	glfwSetCursorPosCallback(window.window, glfw_mouse_position_proc);
	glfwSetWindowSizeCallback(window.window, glfw_window_resize_proc);

	Vulkan::State vk = {};

	Vulkan::init_state(&vk, window.window);
	//	Rendering::init_rendering_state(&vk, &rnd);

	make_game(&vk
		  , &vk.gpu
		  , &vk.swapchain
		  , &window);
	
	
	auto now = std::chrono::high_resolution_clock::now();

	f32 fps = 0.0f;
	
	while(!glfwWindowShouldClose(window.window))
	{
	    glfwPollEvents();
	    update_game(&vk.gpu, &vk.swapchain, &window, &vk, window.dt);
	    
	    auto new_now = std::chrono::high_resolution_clock::now();
	    window.dt = std::chrono::duration<f32, std::chrono::seconds::period>(new_now - now).count();
	    now = new_now;

	    if (glfwGetKey(window.window, GLFW_KEY_R))
	    {
		fps = 1.0f / window.dt;
	    }
	    
	    clear_linear();
	}

	OUTPUT_DEBUG_LOG("stack allocator start address is : %p\n", stack_allocator_global.current);
	OUTPUT_DEBUG_LOG("stack allocator allocated %d bytes\n", (u32)((u8 *)stack_allocator_global.current - (u8 *)stack_allocator_global.start));
	
	OUTPUT_DEBUG_LOG("finished session : FPS : %f\n", fps);

	printf("%f\n", fps);
	
	close_debug_file();

	// destroy rnd and vk
	destroy_game(&vk.gpu);
	
	Vulkan::destroy_state(&vk);
	
	glfwDestroyWindow(window.window);
	glfwTerminate();
    }
    catch(...)
    {
	OUTPUT_DEBUG_LOG("%s\n", "CRASH!!!");
	close_debug_file();
    }
    
    return(0);
}

