#pragma once

#include <cassert>
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>

#define DEBUG true

typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef float f32;
typedef double f64;

typedef u8 byte;

#ifdef __GNUC__
#define FORCEINLINE inline
#else
#define FORCEINLINE __forceinline
#endif

#define MAX(x, y) (((x) > (y)) ? (x) : (y))
#define MIN(x, y) (((x) < (y)) ? (x) : (y))
#define ALLOCA_T(t, c) (t *)alloca(sizeof(t) * c)
#define ALLOCA_B(s) alloca(s)

#define VK_CHECK(f, ...) \
    if (f != VK_SUCCESS) \
    { \
	fprintf(output_file.fp, "[%s:%d] error : %s - ", __FILE__, __LINE__, #f); \
     assert(false);	  \
     } \
    else\
    { \
	fprintf(output_file.fp, "[%s:%d] success : %s\n", __FILE__, __LINE__, #f);	\
    }

#define OUTPUT_DEBUG_LOG(str, ...) \
    fprintf(output_file.fp, "[%s:%d] log: ", __FILE__, __LINE__); \
    fprintf(output_file.fp, str, __VA_ARGS__); \
    fflush(output_file.fp);

#define persist static
#define internal static
#define global_var static

template <typename T>
struct Memory_Buffer_View
{
    u32 count;
    T *buffer;
    
    void
    zero(void)
    {
	memset(buffer, 0, count * sizeof(T));
    }
    
    T &
    operator[](u32 i)
    {
	return(buffer[i]);
    }

    const T &
    operator[](u32 i) const
    {
	return(buffer[i]);
    }
};

inline constexpr u32
left_shift(u32 n)
{
    return 1 << n;
}

extern struct Debug_Output
{
    FILE *fp;
} output_file;

void
output_debug(const char *format
	     , ...);

struct Window_Data
{
    s32 w, h;
    struct GLFWwindow *window;
    f32 dt = 0.0f;

    Memory_Buffer_View<bool> key_map;
    Memory_Buffer_View<bool> mb_map;

    s32 m_x = 0;
    s32 m_y = 0;

    bool m_moved = false;
    bool window_resized = false;
};

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

struct File_Contents
{
    u32 size;
    byte *content;
};

internal File_Contents
read_file(const char *filename
	  , Stack_Allocator *allocator = &stack_allocator_global)
{
    FILE *file = fopen(filename, "rb");
    if (file == nullptr)
    {
	OUTPUT_DEBUG_LOG("error - couldnt load file \"%s\"\n", filename);
	assert(false);
    }
    fseek(file, 0, SEEK_END);
    u32 size = ftell(file);
    rewind(file);

    byte *buffer = (byte *)allocate_stack(size
					  , 1
					  , filename
					  , allocator);
    fread(buffer, 1, size, file);
    
    fclose(file);

    File_Contents contents { size, buffer };
    
    return(contents);
}
 
inline u8
get_alignment_adjust(void *ptr
      , u32 alignment)
{
    byte *byte_cast_ptr = (byte *)ptr;
    u8 adjustment = alignment - reinterpret_cast<u64>(ptr) & static_cast<u64>(alignment - 1);
    if (adjustment == 0) return(0);
    
    return(adjustment);
}

template <typename T>
inline void
destroy(T *ptr
	, u32 size = 1)
{
    for (u32 i = 0
	     ; i < size
	     ; ++i)
    {
	ptr[i].~T();
    }
}

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

#ifndef __GNUC__
#include <intrin.h>
#endif

struct Bitset_32
{
    u32 bitset = 0;

    inline u32
    pop_count(void)
    {
#ifndef __GNUC__
	return __popcnt(bitset);
#else
	return __builtin_popcount(bitset);  
#endif
    }

    inline void
    set1(u32 bit)
    {
	bitset |= left_shift(bit);
    }

    inline void
    set0(u32 bit)
    {
	bitset &= ~(left_shift(bit));
    }

    inline bool
    get(u32 bit)
    {
	return bitset & left_shift(bit);
    }
};

struct Constant_String
{
    const char* str;
    u32 size;
    u32 hash;

    inline bool
    operator==(const Constant_String &other) {return(other.hash == this->hash);}
};

internal inline constexpr u32
compile_hash(const char *string, u32 size)
{
    return ((size ? compile_hash(string, size - 1) : 2166136261u) ^ string[size]) * 16777619u;
}

internal inline constexpr Constant_String
operator""_hash(const char *string, size_t size)
{
    return(Constant_String{string, (u32)size, compile_hash(string, (u32)size)});
}

internal inline Constant_String
init_const_str(const char *str, u32 count)
{
    return(Constant_String{str, count, compile_hash(str, count)});
}

// fast and relatively cheap hash table
template <typename T
	  , u32 Bucket_Count
	  , u32 Bucket_Size
	  , u32 Bucket_Search_Count> struct Hash_Table_Inline
{
    enum { UNINITIALIZED_HASH = 0xFFFFFFFF };
    enum { ITEM_POUR_LIMIT    = Bucket_Search_Count };

    struct Item
    {
	u32 hash = UNINITIALIZED_HASH;
	T value = T();
    };

    struct Bucket
    {
	u32 bucket_usage_count = 0;
	Item items[Bucket_Size] = {};
    };

    const char *map_debug_name;
    Bucket buckets[Bucket_Count] = {};

    Hash_Table_Inline(const char *name) : map_debug_name(name) {}

    void
    insert(u32 hash, T value, const char *debug_name = "")
    {
	u32 start_index = hash % Bucket_Count;
	u32 limit = start_index + ITEM_POUR_LIMIT;
	for (Bucket *bucket = &buckets[start_index]
		 ; bucket->bucket_usage_count != Bucket_Size && start_index < limit
		 ; ++bucket)
	{
	    for (u32 bucket_item = 0
		     ; bucket_item < Bucket_Size
		     ; ++bucket_item)
	    {
		Item *item = &bucket->items[bucket_item];
		if (item->hash == UNINITIALIZED_HASH)
		{
		    /* found a slot for the object */
		    item->hash = hash;
		    item->value = value;
		    return;
		}
	    }
	    OUTPUT_DEBUG_LOG("%s -> %s%s\n", map_debug_name, "hash bucket filled : need bigger buckets! - item name : ", debug_name);
	}

	OUTPUT_DEBUG_LOG("%s -> %s%s\n", map_debug_name, "couldn't fit item into hash because of filled buckets - item name : ", debug_name);
	assert(false);
    }

    void
    remove(u32 hash)
    {
	u32 start_index = hash % Bucket_Count;
	u32 limit = start_index + ITEM_POUR_LIMIT;
	for (Bucket *bucket = &buckets[start_index]
		 ; bucket->bucket_usage_count != Bucket_Size && start_index < limit
		 ; ++bucket)
	{
	    for (u32 bucket_item = 0
		     ; bucket_item < Bucket_Size
		     ; ++bucket_item)
	    {
		Item *item = &bucket->items[bucket_item];
		if (item->hash == UNINITIALIZED_HASH)
		{
		    item->hash = UNINITIALIZED_HASH;
		    item->value = T();
		}
	    }
	}
    }

    T *
    get(u32 hash)
    {
	u32 start_index = hash % Bucket_Count;
	u32 limit = start_index + ITEM_POUR_LIMIT;
	for (Bucket *bucket = &buckets[start_index]
		 ; bucket->bucket_usage_count != Bucket_Size && start_index < limit
		 ; ++bucket)
	{
	    for (u32 bucket_item = 0
		     ; bucket_item < Bucket_Size
		     ; ++bucket_item)
	    {
		Item *item = &bucket->items[bucket_item];
		if (item->hash != UNINITIALIZED_HASH && hash == item->hash)
		{
		    return(&item->value);
		}
	    }
	}
	OUTPUT_DEBUG_LOG("%s -> %s\n", map_debug_name, "failed to find value requested from hash");
	assert(false);
	return(nullptr);
    }
};

template <typename T> internal constexpr Memory_Buffer_View<T>
null_buffer(void) {return(Memory_Buffer_View<T>{0, nullptr});}

template <typename T> internal constexpr Memory_Buffer_View<T>
single_buffer(T *address) {return(Memory_Buffer_View<T>{1, address});}

template <typename T> void
allocate_memory_buffer(Memory_Buffer_View<T> &view, u32 count)
{
    view.count = count;
    view.buffer = (T *)allocate_free_list(count * sizeof(T));
}

template <typename T> void
allocate_memory_buffer_tmp(Memory_Buffer_View<T> &view, u32 count)
{
    view.count = count;
    view.buffer = (T *)allocate_linear(count * sizeof(T));
}

struct Memory_Byte_Buffer
{
    u32 size;
    void *ptr;
};

// predicate needs as param T &
template <typename T, typename Pred> void
loop_through_memory(Memory_Buffer_View<T> &memory
		    , Pred &&predicate)
{
    for (u32 i = 0; i < memory.count; ++i)
    {
	predicate(i);
    }
}

#include <glm/glm.hpp>

const glm::mat4x4 IDENTITY_MAT4X4 = glm::mat4x4(1.0f);
