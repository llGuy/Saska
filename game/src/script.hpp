#pragma once

#include "utils.hpp"

extern "C"
{
    #include <lua.h>
    #include <lauxlib.h>
    #include <lualib.h>
}


enum script_primitive_type_t { NUMBER, STRING, FUNCTION, TABLE_INDEX, TABLE_HANDLE, TABLE_FIELD };
enum stack_item_type_t { GLOBAL, FIELD, ARRAY_INDEX };


struct push_to_stack_info_t
{
    stack_item_type_t type;

    const char *name;

    /* Optional */
    int32_t array_index {0};
    int32_t stack_index = {0};
};


void add_global_function(const char *name, lua_function_t function);
void add_global_number(const char *name, int32_t number);
void add_global_string(const char *name, const char *string);
void get_from_stack(script_primitive_type_t type, int32_t stack_index, void *dst);
void push_to_stack(const push_to_stack_info_t &info);
void execute_lua(const char *code);
void begin_file(const char *filename);
void end_file(void);
void initialize_scripting(void);
void destroy_lua_scripting(void);
