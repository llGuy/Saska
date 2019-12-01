#pragma once

typedef void(*thread_process_t)(void *input_data);

struct mutex_t;

mutex_t *request_mutex(void);
bool wait_for_mutex_and_own(mutex_t *mutex, const char *mutex_name = "");
void release_mutex(mutex_t *mutex, const char *mutex_name ="");

void request_thread_for_process(thread_process_t process, void *input_data);

void initialize_thread_pool(void);



    
