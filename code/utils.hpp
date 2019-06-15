#pragma once

#include <stdint.h>
#include <cstring>
#include <stdio.h>
#include <cassert>

#ifdef __GNUC__
#define FORCEINLINE inline
#else
#define FORCEINLINE __forceinline
#endif

#define persist static
#define internal static
#define global_var static

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

extern struct Debug_Output
{
    FILE *fp;
} output_file; // defined in core.cpp but going to move in the future to a more suitable place

#define OUTPUT_DEBUG_LOG(str, ...)					\
    fprintf(output_file.fp, "[%s:%d] log: ", __FILE__, __LINE__);	\
    fprintf(output_file.fp, str, __VA_ARGS__);				\
    fflush(output_file.fp);

struct Constant_String
{
    const char* str;
    u32 size;
    u32 hash;

    inline bool
    operator==(const Constant_String &other) {return(other.hash == this->hash);}
};

inline constexpr u32
compile_hash(const char *string, u32 size)
{
    return ((size ? compile_hash(string, size - 1) : 2166136261u) ^ string[size]) * 16777619u;
}

inline constexpr Constant_String
operator""_hash(const char *string, size_t size)
{
    return(Constant_String{string, (u32)size, compile_hash(string, (u32)size)});
}

inline Constant_String
init_const_str(const char *str, u32 count)
{
    return(Constant_String{str, count, compile_hash(str, count)});
}

enum Memory_Buffer_View_Allocation { LINEAR, FREE_LIST, STACK };

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
		if (item->hash == hash && item->hash != UNINITIALIZED_HASH)
		{
		    item->hash = UNINITIALIZED_HASH;
		    item->value = T();
		    return;
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
