#pragma once

#include "utils.hpp"
#include "allocators.hpp"

// Goes on the free list allocator
template <typename T> struct heap_array_t
{
    uint32_t array_size;
    T *array;

    void initialize(uint32_t size)
    {
        array_size = size;
        array = (T *)allocate_freelist(sizeof(T) * array_size);
    }

    void deinitialize(void)
    {
        deallocate_freelist(array);
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

enum memory_buffer_view_allocation_t { LINEAR, FREELIST, STACK };

template <typename T, memory_buffer_view_allocation_t Allocation_Type = memory_buffer_view_allocation_t::FREELIST> struct memory_buffer_view_t
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

    void allocate(uint32_t count_in)
    {
        switch (Allocation_Type)
        {
        case memory_buffer_view_allocation_t::LINEAR:
            {
                this->count = count_in;
                buffer = (T *)allocate_linear(sizeof(T) * this->count);
            } break;
        case memory_buffer_view_allocation_t::FREELIST:
            {
                this->count = count_in;
                buffer = (T *)allocate_free_list(sizeof(T) * this->count);
            } break;
        case memory_buffer_view_allocation_t::STACK:
            {
                this->count = count_in;
                buffer = (T *)allocate_stack(sizeof(T) * this->count);
            } break;
        }
    }
};

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
	}


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

	assert(false);
	return(nullptr);
    }
};

typedef enum circular_buffer_ht_difference_tracking_t { NO_TRACKING, TRACKING } cbhtdt_t;

template <typename T, cbhtdt_t CBHTDT = cbhtdt_t::TRACKING> struct circular_buffer_t
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
        buffer[head++] = *item;

        if (head == buffer_size)
        {
            head = 0;
        }

        if constexpr (CBHTDT) { ++head_tail_difference; }
    }

    T *get_next_item(void)
    {
        if constexpr (CBHTDT)
        {
            if (head_tail_difference > 0)
            {
                T *item = &buffer[tail++];

                if (tail == buffer_size)
                {
                    tail = 0;
                }
                
                --head_tail_difference;

                return(item);
            }
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

template <typename T, uint32_t Max, typename Index_Type = uint32_t> struct stack_dynamic_container_t
{
    uint32_t data_count = 0;
    T data[Max];

    uint32_t removed_count = 0;
    Index_Type removed[Max];

    Index_Type add(void)
    {
        if (removed_count)
        {
            return removed[removed_count-- - 1];
        }
        else
        {
            return data_count++;
        }
    }

    T *get(Index_Type index)
    {
        return &data[index];
    }

    void remove(Index_Type index)
    {
        removed[removed_count++] = index;
    }
};

// Guess it's a sort of "data" container ...
struct bitset32_t
{
    uint32_t bitset = 0;

    inline uint32_t pop_count(void)
    {
#ifndef __GNUC__
	return __popcnt(bitset);
#else
	return __builtin_popcount(bitset);  
#endif
    }

    inline void set1(uint32_t bit)
    {
	bitset |= (1 << bit);
    }

    inline void set0(uint32_t bit)
    {
	bitset &= ~(1 << bit);
    }

    inline bool get(uint32_t bit)
    {
	return bitset & (1 << bit);
    }
};

struct memory_byte_buffer_t
{
    uint32_t size;
    void *ptr;
};
