#include "memory.hpp"
#include <stdlib.h>
#include <cassert>

MEMORY_API inline uint8_t get_alignment_adjust(void *ptr, uint32_t alignment)
{
    byte_t *byte_cast_ptr = (byte_t *)ptr;
    uint8_t adjustment = alignment - reinterpret_cast<uint64_t>(ptr) & static_cast<uint64_t>(alignment - 1);
    if (adjustment == 0) return(0);
    
    return(adjustment);
}

MEMORY_API void *allocate_linear_impl(uint32_t alloc_size, alignment_t alignment, const char *name, linear_allocator_t *allocator)
{
    void *prev = allocator->current;
    void *new_crnt = (byte_t *)allocator->current + alloc_size;

    allocator->current = new_crnt;
    allocator->used_capacity += alloc_size;

    return(prev);
}

MEMORY_API void clear_linear_impl(linear_allocator_t *allocator)
{
    allocator->current = allocator->start;
}

MEMORY_API void *allocate_stack_impl(uint32_t allocation_size, alignment_t alignment, const char *name, stack_allocator_t *allocator)
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

MEMORY_API void extend_stack_top_impl(uint32_t extension_size, stack_allocator_t *allocator)
{
    stack_allocation_header_t *current_header = (stack_allocation_header_t *)allocator->current;
    current_header->size += extension_size;
}

MEMORY_API void pop_stack_impl(stack_allocator_t *allocator)
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

MEMORY_API void *allocate_free_list_impl(uint32_t allocation_size, alignment_t alignment, const char *name, free_list_allocator_t *allocator)
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
MEMORY_API void deallocate_free_list_impl(void *pointer, free_list_allocator_t *allocator)
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
