#pragma once

#include "utils.hpp"

inline constexpr uint64_t
kilobytes(uint32_t kb)
{
    return(kb * 1024);
}

inline constexpr uint64_t
megabytes(uint32_t mb)
{
    return(kilobytes(mb * 1024));
}

using alignment_t = uint8_t;

extern struct linear_allocator_t
{
    void *start = nullptr;
    void *current = nullptr;

    uint32_t capacity;
} linear_allocator_global;

void *
allocate_linear(uint32_t alloc_size
		, alignment_t alignment = 1
		, const char *name = ""
		, linear_allocator_t *allocator = &linear_allocator_global);

void
clear_linear(linear_allocator_t *allocator = &linear_allocator_global);

struct stack_allocation_header_t
{
#if DEBUG
    const char *allocation_name;
#endif
    
    uint32_t size;
    void *prev;
};

extern struct stack_allocator_t
{
    void *start = nullptr;
    void *current = nullptr;
    
    uint32_t allocation_count = 0;
    uint32_t capacity;
} stack_allocator_global;

void *
allocate_stack(uint32_t allocation_size
	       , alignment_t alignment = 1
	       , const char *name = ""
	       , stack_allocator_t *allocator = &stack_allocator_global);

// only applies to the allocation at the top of the stack
void
extend_stack_top(uint32_t extension_size
		 , stack_allocator_t *allocator = &stack_allocator_global);

// contents in the allocation must be destroyed by the user
void
pop_stack(stack_allocator_t *allocator = &stack_allocator_global);

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

extern struct free_list_allocator_t
{
    free_block_header_t *free_block_head;
    
    void *start;
    uint32_t available_bytes;

    uint32_t allocation_count = 0;
} free_list_allocator_global;

void *
allocate_free_list(uint32_t allocation_size
		   , alignment_t alignment = 1
		   , const char *name = ""
		   , free_list_allocator_t *allocator = &free_list_allocator_global);

void
deallocate_free_list(void *pointer
		     , free_list_allocator_t *allocator = &free_list_allocator_global);

void
init_manager(void);

void
decrease_shared_count(const constant_string_t &id);

void
increase_shared_count(const constant_string_t &id);    

// CPU memory used for allocating vulkan / gpu objects (images, vbos, ...)
struct registered_memory_base_t
{
    // if p == nullptr, the object was deleted
    void *p;
    constant_string_t id;

    uint32_t size;

    registered_memory_base_t(void) = default;
	
    registered_memory_base_t(void *p, const constant_string_t &id, uint32_t size)
	: p(p), id(id), size(size) {increase_shared_count(id);}

    ~registered_memory_base_t(void) {if (p) {decrease_shared_count(id);}}
};
/*
registered_memory_base_t
register_memory(const constant_string_t &id
		, uint32_t size);

registered_memory_base_t
register_existing_memory(void *ptr
			 , const constant_string_t &id
			 , uint32_t size);

registered_memory_base_t
get_memory(const constant_string_t &id);

void
move_memory(void *p, uint32_t size, const constant_string_t &id);

void
deregister_memory(const constant_string_t &id);

void
deregister_memory_and_deallocate(const constant_string_t &id);
*/

// basically just a pointer
template <typename T> struct registered_memory_t
{
    using My_Type = registered_memory_t<T>;
	
    T *p;
    constant_string_t id;

    uint32_t size;

    FORCEINLINE void
    destroy(void) {p = nullptr; decrease_shared_count(id);};

    // to use only if is array
    FORCEINLINE My_Type
    extract(uint32_t i) {return(My_Type(registered_memory_base_t(p + i, id, sizeof(T))));};

    FORCEINLINE memory_buffer_view_t<My_Type>
    separate(void)
    {
	memory_buffer_view_t<My_Type> view;
	allocate_memory_buffer(view, size);

	for (uint32_t i = 0; i < size; ++i) view.buffer[i] = extract(i);

	return(view);
    }

    FORCEINLINE T *
    operator->(void) {return(p);}
	
    registered_memory_t(void) = default;
    registered_memory_t(void *p, const constant_string_t &id, uint32_t size) = delete;
    registered_memory_t(const My_Type &in) : p((T *)in.p), id(in.id), size(in.size / sizeof(T)) {increase_shared_count(id);};
    registered_memory_t(const registered_memory_base_t &in) : p((T *)in.p), id(in.id), size(in.size / sizeof(T)) {increase_shared_count(id);}
    registered_memory_t(registered_memory_base_t &&in) : p((T *)in.p), id(in.id), size(in.size / sizeof(T)) {in.p = nullptr;}
    My_Type &operator=(const My_Type &c) {this->p = c.p; this->id = c.id; this->size = c.size; increase_shared_count(id); return(*this);}
    My_Type &operator=(My_Type &&m)	{this->p = m.p; this->id = m.id; this->size = m.size; m.p = nullptr; return(*this);}

    T &operator[](uint32_t i) {return(p[i]);};

    FORCEINLINE memory_buffer_view_t<T>
    to_memory_buffer_view(void)
    {
	return(memory_buffer_view_t<T>{size, p});
    }
    
    ~registered_memory_t(void) {if (p) {decrease_shared_count(id);}}
};

// piece of memory that is registered with a name
//template <typename T> using r_mem_t = Registered_Memory<T>; 
