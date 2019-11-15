#include <Windows.h>
#include "thread_pool.hpp"
#include "utils.hpp"

struct thread_t
{
    HANDLE thread_handle;
    DWORD thread_id;

    bool requested = 0;
    
    thread_process_t process;
    void *input_data;
};

#define MAX_THREAD_COUNT 5

struct thread_pool_t
{
    uint32_t active_thread_count = 0;
    thread_t active_threads[MAX_THREAD_COUNT];
} g_thread_pool;

DWORD WINAPI thread_process_impl(LPVOID lp_parameter)
{
    thread_t *thread = (thread_t *)lp_parameter;
    
    for (;;)
    {
        // TODO: Add mutex
        if (thread->requested)
        {
            thread->process(thread->input_data);
            thread->requested = 0;
        }
    }

    return(0);
}

thread_t *get_next_available_thread(void)
{
    for (uint32_t i = 0; i < g_thread_pool.active_thread_count; ++i)
    {
        thread_t *current_thread = &g_thread_pool.active_threads[i];
        if (!current_thread->requested)
        {
            return(current_thread);
        }
    }
    return(nullptr);
}

void request_thread_for_process(thread_process_t process, void *input_data)
{
    thread_t *thread = get_next_available_thread();
    if (!thread)
    {
        if (g_thread_pool.active_thread_count < MAX_THREAD_COUNT)
        {
            thread = &g_thread_pool.active_threads[g_thread_pool.active_thread_count++];
            thread->thread_handle = CreateThread(0, 0, thread_process_impl, thread, 0, &thread->thread_id);
        }
        else
        {
            OutputDebugString("Error: Unable to find available thread to use for process");
        }
    }
}

void initialize_thread_pool(void)
{
}

