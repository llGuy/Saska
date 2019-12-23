#pragma once

#include <stdint.h>

inline constexpr uint64_t kilobytes(uint32_t kb)
{
    return(kb * 1024);
}

inline constexpr uint64_t megabytes(uint32_t mb)
{
    return(kilobytes(mb * 1024));
}

using alignment_t = uint8_t;

// TODO: Sort out issues with allocating with this thing
struct linear_allocator_t
{
    void *start = nullptr;
    void *current = nullptr;

    uint32_t capacity;
    uint32_t used_capacity = 0;
};

void *allocate_linear_impl(uint32_t alloc_size, alignment_t alignment, const char *name, linear_allocator_t *allocator);
void clear_linear_impl(linear_allocator_t *allocator);

struct stack_allocation_header_t
{
#if DEBUG
    const char *allocation_name;
#endif
    
    uint32_t size;
    void *prev;
};

struct stack_allocator_t
{
    void *start = nullptr;
    void *current = nullptr;
    
    uint32_t allocation_count = 0;
    uint32_t capacity;
};

void *allocate_stack_impl(uint32_t allocation_size, alignment_t alignment, const char *name, stack_allocator_t *allocator);

// only applies to the allocation at the top of the stack
void extend_stack_top_impl(uint32_t extension_size, stack_allocator_t *allocator);
// contents in the allocation must be destroyed by the user
void pop_stack_impl(stack_allocator_t *allocator);

struct free_block_header_t
{
    free_block_header_t *next_free_block = nullptr;
    uint32_t free_block_size = 0;
};

struct free_list_allocation_header_t
{
    uint32_t size;
#if DEBUG
    const char *name;
#endif
};

struct free_list_allocator_t
{
    free_block_header_t *free_block_head;
    
    void *start;
    uint32_t available_bytes;

    uint32_t allocation_count = 0;

    uint32_t used_memory = 0;
};

void *allocate_free_list_impl(uint32_t allocation_size, alignment_t alignment, const char *name, free_list_allocator_t *allocator);
void deallocate_free_list_impl(void *pointer, free_list_allocator_t *allocator);
