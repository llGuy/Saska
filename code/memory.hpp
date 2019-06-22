#pragma once

#include "utils.hpp"

inline constexpr u64
kilobytes(u32 kb)
{
    return(kb * 1024);
}

inline constexpr u64
megabytes(u32 mb)
{
    return(kilobytes(mb * 1024));
}

using Alignment = u8;

extern struct Linear_Allocator
{
    void *start = nullptr;
    void *current = nullptr;

    u32 capacity;
} linear_allocator_global;

void *
allocate_linear(u32 alloc_size
		, Alignment alignment = 1
		, const char *name = ""
		, Linear_Allocator *allocator = &linear_allocator_global);

void
clear_linear(Linear_Allocator *allocator = &linear_allocator_global);

struct Stack_Allocation_Header
{
#if DEBUG
    const char *allocation_name;
#endif
    
    u32 size;
    void *prev;
};

extern struct Stack_Allocator
{
    void *start = nullptr;
    void *current = nullptr;
    
    u32 allocation_count = 0;
    u32 capacity;
} stack_allocator_global;

void *
allocate_stack(u32 allocation_size
	       , Alignment alignment = 1
	       , const char *name = ""
	       , Stack_Allocator *allocator = &stack_allocator_global);

// only applies to the allocation at the top of the stack
void
extend_stack_top(u32 extension_size
		 , Stack_Allocator *allocator = &stack_allocator_global);

// contents in the allocation must be destroyed by the user
void
pop_stack(Stack_Allocator *allocator = &stack_allocator_global);

struct Free_Block_Header
{
    Free_Block_Header *next_free_block = nullptr;
    u32 free_block_size = 0;
};

struct Free_List_Allocation_Header
{
    u32 size;
#if DEBUG
    const char *name;
#endif
};

extern struct Free_List_Allocator
{
    Free_Block_Header *free_block_head;
    
    void *start;
    u32 available_bytes;

    u32 allocation_count = 0;
} free_list_allocator_global;

void *
allocate_free_list(u32 allocation_size
		   , Alignment alignment = 1
		   , const char *name = ""
		   , Free_List_Allocator *allocator = &free_list_allocator_global);

void
deallocate_free_list(void *pointer
		     , Free_List_Allocator *allocator = &free_list_allocator_global);

void
init_manager(void);

void
decrease_shared_count(const Constant_String &id);

void
increase_shared_count(const Constant_String &id);    

// CPU memory used for allocating vulkan / gpu objects (images, vbos, ...)
struct Registered_Memory_Base
{
    // if p == nullptr, the object was deleted
    void *p;
    Constant_String id;

    u32 size;

    Registered_Memory_Base(void) = default;
	
    Registered_Memory_Base(void *p, const Constant_String &id, u32 size)
	: p(p), id(id), size(size) {increase_shared_count(id);}

    ~Registered_Memory_Base(void) {if (p) {decrease_shared_count(id);}}
};
/*
Registered_Memory_Base
register_memory(const Constant_String &id
		, u32 size);

Registered_Memory_Base
register_existing_memory(void *ptr
			 , const Constant_String &id
			 , u32 size);

Registered_Memory_Base
get_memory(const Constant_String &id);

void
move_memory(void *p, u32 size, const Constant_String &id);

void
deregister_memory(const Constant_String &id);

void
deregister_memory_and_deallocate(const Constant_String &id);
*/

// basically just a pointer
template <typename T> struct Registered_Memory
{
    using My_Type = Registered_Memory<T>;
	
    T *p;
    Constant_String id;

    u32 size;

    FORCEINLINE void
    destroy(void) {p = nullptr; decrease_shared_count(id);};

    // to use only if is array
    FORCEINLINE My_Type
    extract(u32 i) {return(My_Type(Registered_Memory_Base(p + i, id, sizeof(T))));};

    FORCEINLINE Memory_Buffer_View<My_Type>
    separate(void)
    {
	Memory_Buffer_View<My_Type> view;
	allocate_memory_buffer(view, size);

	for (u32 i = 0; i < size; ++i) view.buffer[i] = extract(i);

	return(view);
    }

    FORCEINLINE T *
    operator->(void) {return(p);}
	
    Registered_Memory(void) = default;
    Registered_Memory(void *p, const Constant_String &id, u32 size) = delete;
    Registered_Memory(const My_Type &in) : p((T *)in.p), id(in.id), size(in.size / sizeof(T)) {increase_shared_count(id);};
    Registered_Memory(const Registered_Memory_Base &in) : p((T *)in.p), id(in.id), size(in.size / sizeof(T)) {increase_shared_count(id);}
    Registered_Memory(Registered_Memory_Base &&in) : p((T *)in.p), id(in.id), size(in.size / sizeof(T)) {in.p = nullptr;}
    My_Type &operator=(const My_Type &c) {this->p = c.p; this->id = c.id; this->size = c.size; increase_shared_count(id); return(*this);}
    My_Type &operator=(My_Type &&m)	{this->p = m.p; this->id = m.id; this->size = m.size; m.p = nullptr; return(*this);}

    T &operator[](u32 i) {return(p[i]);};

    FORCEINLINE Memory_Buffer_View<T>
    to_memory_buffer_view(void)
    {
	return(Memory_Buffer_View<T>{size, p});
    }
    
    ~Registered_Memory(void) {if (p) {decrease_shared_count(id);}}
};

// piece of memory that is registered with a name
//template <typename T> using R_Mem = Registered_Memory<T>; 
