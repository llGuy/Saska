#pragma once

#include "memory.hpp"

extern linear_allocator_t linear_allocator_global;
extern stack_allocator_t stack_allocator_global;
extern free_list_allocator_t free_list_allocator_global;

inline void *allocate_linear(uint32_t alloc_size, alignment_t alignment = 1, const char *name = "", linear_allocator_t *allocator = &linear_allocator_global) {
    return(allocate_linear_impl(alloc_size, alignment, name, allocator));
}

inline void clear_linear(linear_allocator_t *allocator = &linear_allocator_global) {
    clear_linear_impl(allocator);
}

inline void *allocate_stack(uint32_t allocation_size, alignment_t alignment = 1, const char *name = "", stack_allocator_t *allocator = &stack_allocator_global) {
    return(allocate_stack_impl(allocation_size, alignment, name, allocator));
}

// only applies to the allocation at the top of the stack
inline void extend_stack_top(uint32_t extension_size, stack_allocator_t *allocator = &stack_allocator_global) {
    extend_stack_top_impl(extension_size, allocator);
}

// contents in the allocation must be destroyed by the user
inline void pop_stack(stack_allocator_t *allocator = &stack_allocator_global) {
    pop_stack_impl(allocator);
}

inline void *allocate_free_list(uint32_t allocation_size, alignment_t alignment = 1, const char *name = "", free_list_allocator_t *allocator = &free_list_allocator_global) {
    return(allocate_free_list_impl(allocation_size, alignment, name, allocator));
}

inline void deallocate_free_list(void *pointer, free_list_allocator_t *allocator = &free_list_allocator_global) {
    deallocate_free_list_impl(pointer, allocator);
}


#define FL_MALLOC(type, n) (type *)allocate_free_list(sizeof(type) * n)
#define LN_MALLOC(type, n) (type *)allocate_linear(sizeof(type) * n)
