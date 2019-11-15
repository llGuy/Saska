#pragma once

typedef void(*thread_process_t)(void *input_data);

void request_thread_for_process(thread_process_t process, void *input_data);

void initialize_thread_pool(void);



    
