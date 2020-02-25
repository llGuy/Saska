#include <Windows.h>
#include "thread_pool.hpp"
#include "utils.hpp"

struct mutex_t {
    HANDLE mutex_handle;
};

struct thread_t {
    HANDLE thread_handle;
    DWORD thread_id;

    bool requested = 0;
    
    thread_process_t process;
    void *input_data;

    mutex_t mutex;
};

#define MAX_THREAD_COUNT 5
#define MAX_MUTEX_COUNT 10

struct thread_pool_t {
    uint32_t active_thread_count = 0;
    thread_t active_threads[MAX_THREAD_COUNT];

    // Requestable mutexes
    uint32_t used_mutexes_count = 0;
    mutex_t mutexes[MAX_MUTEX_COUNT];
} g_thread_pool;

DWORD WINAPI thread_process_impl(LPVOID lp_parameter) {
    thread_t *thread = (thread_t *)lp_parameter;
    
    for (;;) {
        wait_for_mutex_and_own(&thread->mutex, "thread->requested"); // Mutex which acts on thread->requested
        if (thread->requested) {
            thread->process(thread->input_data);
            thread->requested = 0;
        }
        release_mutex(&thread->mutex, "thread->requested");
    }

    return(0);
}

thread_t *get_next_available_thread(void) {
    for (uint32_t i = 0; i < g_thread_pool.active_thread_count; ++i) {
        thread_t *current_thread = &g_thread_pool.active_threads[i];
        if (!current_thread->requested) {
            return(current_thread);
        }
    }
    return(nullptr);
}

void create_mutex(mutex_t *mutex) {
    mutex->mutex_handle = CreateMutex(NULL, FALSE, NULL);
}

mutex_t *request_mutex(void) {
    if (g_thread_pool.used_mutexes_count < MAX_MUTEX_COUNT) {
        mutex_t *mutex = &g_thread_pool.mutexes[g_thread_pool.used_mutexes_count++];
        create_mutex(mutex);

        if (mutex->mutex_handle) {
            return(mutex);
        }
        return(nullptr);
    }
    
    return(nullptr);
}

bool wait_for_mutex_and_own(mutex_t *mutex, const char *mutex_name) {
    DWORD result = WaitForSingleObject(mutex->mutex_handle, INFINITE);

    switch(result) {
    case WAIT_OBJECT_0: {
            return(1);
        } break;
    case WAIT_ABANDONED: {
            return(0);
        } break;
    }

    return(0);
}

void release_mutex(mutex_t *mutex, const char *mutex_name) {
    ReleaseMutex(mutex->mutex_handle);
}

void request_thread_for_process(thread_process_t process, void *input_data) {
    thread_t *thread = get_next_available_thread();
    if (!thread) {
        if (g_thread_pool.active_thread_count < MAX_THREAD_COUNT) {
            thread = &g_thread_pool.active_threads[g_thread_pool.active_thread_count++];
            create_mutex(&thread->mutex);
            thread->thread_handle = CreateThread(0, 0, thread_process_impl, thread, 0, &thread->thread_id);
        }
        else {
            OutputDebugString("Error: Unable to find available thread to use for process");
        }
    }

    thread->process = process;
    thread->input_data = input_data;
    
    wait_for_mutex_and_own(&thread->mutex, "thread->requested");
    thread->requested = 1;
    release_mutex(&thread->mutex, "thread->requested");
}

void initialize_thread_pool(void) {
    
}

