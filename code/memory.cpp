#include "memory.hpp"
#include <stdlib.h>
#include <cassert>

linear_allocator_t linear_allocator_global;
stack_allocator_t stack_allocator_global;
free_list_allocator_t free_list_allocator_global;

inline uint8_t get_alignment_adjust(void *ptr, uint32_t alignment)
{
    byte_t *byte_cast_ptr = (byte_t *)ptr;
    uint8_t adjustment = alignment - reinterpret_cast<uint64_t>(ptr) & static_cast<uint64_t>(alignment - 1);
    if (adjustment == 0) return(0);
    
    return(adjustment);
}

void *allocate_linear(uint32_t alloc_size, alignment_t alignment, const char *name, linear_allocator_t *allocator)
{
    void *prev = allocator->current;
    void *new_crnt = (byte_t *)allocator->current + alloc_size;

    allocator->current = new_crnt;

    return(prev);
}

void clear_linear(linear_allocator_t *allocator)
{
    allocator->current = allocator->start;
}

void *allocate_stack(uint32_t allocation_size, alignment_t alignment, const char *name, stack_allocator_t *allocator)
{
    byte_t *would_be_address;

    if (allocator->allocation_count == 0) would_be_address = (uint8_t *)allocator->current + sizeof(stack_allocation_header_t);
    else would_be_address = (uint8_t *)allocator->current
	     + sizeof(stack_allocation_header_t) * 2 
	     + ((stack_allocation_header_t *)(allocator->current))->size;
    
    // calculate the aligned address that is needed
    uint8_t alignment_adjustment = get_alignment_adjust(would_be_address
						   , alignment);

    // start address of (header + allocation)
    byte_t *start_address = (would_be_address + alignment_adjustment) - sizeof(stack_allocation_header_t);
    assert((start_address + sizeof(stack_allocation_header_t) + allocation_size) < (uint8_t *)allocator->start + allocator->capacity);

    stack_allocation_header_t *header = (stack_allocation_header_t *)start_address;
    
#if DEBUG
    //header->allocation_name = name;
    //    OUTPUT_DEBUG_LOG("stack allocation for \"%s\"\n", name);
#endif

    header->size = allocation_size;
    header->prev = allocator->allocation_count == 0 ? nullptr : (stack_allocation_header_t *)allocator->current;

    allocator->current = (void *)header;
    ++(allocator->allocation_count);

    return(start_address + sizeof(stack_allocation_header_t));
}

void extend_stack_top(uint32_t extension_size, stack_allocator_t *allocator)
{
    stack_allocation_header_t *current_header = (stack_allocation_header_t *)allocator->current;
    current_header->size += extension_size;
}

void pop_stack(stack_allocator_t *allocator)
{
    stack_allocation_header_t *current_header = (stack_allocation_header_t *)allocator->current;
    
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
			 , ((stack_allocation_header_t *)(current_header->prev))->allocation_name);
    }
#endif

    if (allocator->allocation_count == 1) allocator->current = allocator->start;
    else allocator->current = current_header->prev;
    --(allocator->allocation_count);
}

void *allocate_free_list(uint32_t allocation_size, alignment_t alignment, const char *name, free_list_allocator_t *allocator)
{
    uint32_t total_allocation_size = allocation_size + sizeof(free_list_allocation_header_t);

    allocator->used_memory += total_allocation_size;
    
    // find best fit free block
    // TODO(luc) : make free list allocator adjust the smallest free block according to the alignment as well
    free_block_header_t *previous_free_block = nullptr;
    free_block_header_t *smallest_free_block = allocator->free_block_head;
    for (free_block_header_t *header = allocator->free_block_head; header; header = header->next_free_block)
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
    free_block_header_t *next = smallest_free_block->next_free_block;
    if (previous_free_block)
    {
	uint32_t previous_smallest_block_size = smallest_free_block->free_block_size;
	previous_free_block->next_free_block = (free_block_header_t *)(((byte_t *)smallest_free_block) + total_allocation_size);
	previous_free_block->next_free_block->free_block_size = previous_smallest_block_size - total_allocation_size;
	previous_free_block->next_free_block->next_free_block = next;
    }
    else
    {
	free_block_header_t *new_block = (free_block_header_t *)(((byte_t *)smallest_free_block) + total_allocation_size);
	allocator->free_block_head = new_block;
	new_block->free_block_size = smallest_free_block->free_block_size - total_allocation_size;
	new_block->next_free_block = smallest_free_block->next_free_block;
    }
    
    free_list_allocation_header_t *header = (free_list_allocation_header_t *)smallest_free_block;
    header->size = allocation_size;
#if DEBUG
    //    header->name = name;
#endif
    return((byte_t *)header + sizeof(free_list_allocation_header_t));
}

// TODO(luc) : optimize free list allocator so that all the blocks are stored in order
void
deallocate_free_list(void *pointer
		     , free_list_allocator_t *allocator)
{
    free_list_allocation_header_t *allocation_header = (free_list_allocation_header_t *)((byte_t *)pointer - sizeof(free_list_allocation_header_t));
    free_block_header_t *new_free_block_header = (free_block_header_t *)allocation_header;
    new_free_block_header->free_block_size = allocation_header->size + sizeof(free_block_header_t);

    free_block_header_t *previous = nullptr;
    free_block_header_t *current_header = allocator->free_block_head;

    free_block_header_t *viable_prev = nullptr;
    free_block_header_t *viable_next = nullptr;
    
    // check if possible to merge free blocks
    bool merged = false;
    for (; current_header; current_header = current_header->next_free_block)
    {
	if (new_free_block_header > previous && new_free_block_header < current_header)
	{
	    viable_prev = previous;
	    viable_next = current_header;
	}
	
	// check if free blocks will overlap
	// does the header go over the newly freed header
	if (current_header->free_block_size + (byte_t *)current_header >= (byte_t *)new_free_block_header
	    && (byte_t *)current_header < (byte_t *)new_free_block_header)
	{
	    current_header->free_block_size = ((byte_t *)new_free_block_header + new_free_block_header->free_block_size) - (byte_t *)current_header;
	    new_free_block_header = current_header;
	    previous = current_header;
	    merged = true;
	    continue;
	}
	// does the newly freed header go over the header
	if ((byte_t *)current_header <= (byte_t *)new_free_block_header + new_free_block_header->free_block_size
	    && (byte_t *)current_header > (byte_t *)new_free_block_header)
	{
	    // if current header is not the head of the list
	    new_free_block_header->free_block_size = (byte_t *)((byte_t *)current_header + current_header->free_block_size) - (byte_t *)new_free_block_header; 
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
	free_block_header_t *old_head = allocator->free_block_head;
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

struct memory_register_info_t
{
    uint32_t active_count = 0;
    void *p; // pointer to the actual object
    uint32_t size;
};

global_var free_list_allocator_t object_allocator;
global_var hash_table_inline_t<memory_register_info_t /* destroyed count */, 20, 4, 4> objects_list("map.object_removed_list");

void init_manager(void)
{
    object_allocator.start = malloc(megabytes(10));
    object_allocator.available_bytes = megabytes(10);
    memset(object_allocator.start, 0, object_allocator.available_bytes);
	
    object_allocator.free_block_head = (free_block_header_t *)object_allocator.start;
    object_allocator.free_block_head->free_block_size = object_allocator.available_bytes;
}
    
registered_memory_base_t register_memory(const constant_string_t &id, uint32_t bytes_size)
{
    void *p = allocate_free_list(bytes_size, 1, "", &object_allocator);

    memory_register_info_t register_info {0, p, bytes_size};
    objects_list.insert(id.hash, register_info, "");

    return(registered_memory_base_t(p, id, bytes_size));
}

registered_memory_base_t register_existing_memory(void *p, const constant_string_t &id, uint32_t bytes_size)
{
    memory_register_info_t register_info {0, p, bytes_size};
    objects_list.insert(id.hash, register_info, "");

    return(registered_memory_base_t(p, id, bytes_size));
}

void deregister_memory(const constant_string_t &id)
{
    objects_list.remove(id.hash);
}

registered_memory_base_t get_memory(const constant_string_t &id)
{
    memory_register_info_t *info = objects_list.get(id.hash);

    if (!info)
    {
	//	OUTPUT_DEBUG_LOG("unable to find element %s\n", id.str);
    }
	
    return(registered_memory_base_t(info->p, id, info->size));
}

void remove_memory(const constant_string_t &id)
{
    memory_register_info_t *info = objects_list.get(id.hash);
    if (info->active_count == 0)
    {
	deallocate_free_list(info->p, &object_allocator);
    }
    else
    {
	// error, cannot delete object yet
    }
}

void decrease_shared_count(const constant_string_t &id)
{
    memory_register_info_t *ri = objects_list.get(id.hash);
    --ri->active_count;
}

void increase_shared_count(const constant_string_t &id)
{
    memory_register_info_t *ri = objects_list.get(id.hash);
    ++ri->active_count;
}
