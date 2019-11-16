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

#define persist_var static
#define internal_function static
#define global_var static

#include <glm/glm.hpp>

typedef glm::vec2 vector2_t;
typedef glm::vec3 vector3_t;
typedef glm::vec4 vector4_t;
typedef glm::ivec2 ivector2_t;
typedef glm::ivec3 ivector3_t;
typedef glm::ivec4 ivector4_t;
typedef glm::mat3 matrix3_t;
typedef glm::mat4 matrix4_t;
typedef glm::quat quaternion_t;

typedef float float32_t;
typedef double float64_t;

typedef uint8_t byte_t;
typedef uint32_t bool32_t;

#define OUTPUT_DEBUG_LOG(str, ...)

struct constant_string_t
{
    const char* str;
    uint32_t size;
    uint32_t hash;

    inline bool
    operator==(const constant_string_t &other) {return(other.hash == this->hash);}
};

inline constexpr uint32_t compile_hash(const char *string, uint32_t size)
{
    return ((size ? compile_hash(string, size - 1) : 2166136261u) ^ string[size]) * 16777619u;
}

inline constexpr constant_string_t operator""_hash(const char *string, size_t size)
{
    return(constant_string_t{string, (uint32_t)size, compile_hash(string, (uint32_t)size)});
}

inline constant_string_t make_constant_string(const char *str, uint32_t count)
{
    return(constant_string_t{str, count, compile_hash(str, count)});
}

inline constant_string_t init_const_str(const char *str, uint32_t count)
{
    return(constant_string_t{str, count, compile_hash(str, count)});
}

template <typename T> struct heap_array_t
{
    uint32_t array_size;
    T *array;

    void initialize(uint32_t size)
    {
        array_size = size;
        array = (T *)allocate_free_list(sizeof(T) * array_size);
    }

    void deinitialize(void)
    {
        deallocate_free_list(array);
    }

    T &operator[](uint32_t index)
    {
        // TODO: May need to remove this for release mode
        if (index >= array_size)
        {
            assert(0);
        }
        
        return(array[index]);
    }
};

enum memory_buffer_view_allocation_t { LINEAR, FREE_LIST, STACK };

template <typename T>
struct memory_buffer_view_t
{
    uint32_t count;
    T *buffer;
    
    void zero(void)
    {
	memset(buffer, 0, count * sizeof(T));
    }
    
    T &operator[](uint32_t i)
    {
	return(buffer[i]);
    }

    const T &operator[](uint32_t i) const
    {
	return(buffer[i]);
    }
};
//"
// fast and relatively cheap hash table
template <typename T, uint32_t Bucket_Count, uint32_t Bucket_Size, uint32_t Bucket_Search_Count> struct hash_table_inline_t
{
    enum { UNINITIALIZED_HASH = 0xFFFFFFFF };
    enum { ITEM_POUR_LIMIT    = Bucket_Search_Count };

    struct item_t
    {
	uint32_t hash = UNINITIALIZED_HASH;
	T value = T();
    };

    struct bucket_t
    {
	uint32_t bucket_usage_count = 0;
	item_t items[Bucket_Size] = {};
    };

    const char *map_debug_name;
    bucket_t buckets[Bucket_Count] = {};

    hash_table_inline_t(void) = default;
    hash_table_inline_t(const char *name) : map_debug_name(name) {}

    void clean_up(void)
    {
        for (uint32_t i = 0; i < Bucket_Count; ++i)
        {
            buckets[i].bucket_usage_count = 0;
        }
    }

    void insert(uint32_t hash, T value, const char *debug_name = "")
    {
	uint32_t start_index = hash % Bucket_Count;
	uint32_t limit = start_index + ITEM_POUR_LIMIT;
	for (bucket_t *bucket = &buckets[start_index]; bucket->bucket_usage_count != Bucket_Size && start_index < limit; ++bucket)
	{
	    for (uint32_t bucket_item = 0; bucket_item < Bucket_Size; ++bucket_item)
	    {
		item_t *item = &bucket->items[bucket_item];
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

    void remove(uint32_t hash)
    {
	uint32_t start_index = hash % Bucket_Count;
	uint32_t limit = start_index + ITEM_POUR_LIMIT;
	for (bucket_t *bucket = &buckets[start_index]; bucket->bucket_usage_count != Bucket_Size && start_index < limit; ++bucket)
	{
	    for (uint32_t bucket_item = 0; bucket_item < Bucket_Size; ++bucket_item)
	    {
		item_t *item = &bucket->items[bucket_item];
		if (item->hash == hash && item->hash != UNINITIALIZED_HASH)
		{
		    item->hash = UNINITIALIZED_HASH;
		    item->value = T();
		    return;
		}
	    }
	}
    }

    T *get(uint32_t hash)
    {
	uint32_t start_index = hash % Bucket_Count;
	uint32_t limit = start_index + ITEM_POUR_LIMIT;
	for (bucket_t *bucket = &buckets[start_index]; bucket->bucket_usage_count != Bucket_Size && start_index < limit; ++bucket)
	{
	    for (uint32_t bucket_item = 0; bucket_item < Bucket_Size; ++bucket_item)
	    {
		item_t *item = &bucket->items[bucket_item];
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

template <typename T> struct circular_buffer_t
{
    uint32_t head_tail_difference = 0;
    uint32_t head = 0;
    uint32_t tail = 0;
    uint32_t buffer_size;
    T *buffer;

    void initialize(uint32_t count)
    {
        buffer_size = count;
        uint32_t byte_count = buffer_size * sizeof(T);
        buffer = (T *)allocate_free_list(byte_count);
    }

    void push_item(T *item)
    {
        if (head == buffer_size)
        {
            head = 0;
        }
        buffer[head++] = *item;
        ++head_tail_difference;
    }

    T *get_next_item(void)
    {
        if (head_tail_difference > 0)
        {
            if (tail == buffer_size)
            {
                tail = 0;
            }
    
            T *item = &buffer[tail++];
            --head_tail_difference;

            return(item);
        }

        return(nullptr);
    }

    void deinitialize(void)
    {
        if (buffer)
        {
            deallocate_free_list(buffer);
        }
        buffer = nullptr;
        buffer_size = 0;
        head = 0;
        tail = 0;
        head_tail_difference = 0;
    }
};


