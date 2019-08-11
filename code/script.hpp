#pragma once

#include "utils.hpp"
#include "vulkan.hpp"

extern "C"
{
    #include <lua.h>
    #include <lauxlib.h>
    #include <lualib.h>
}

extern struct lua_State *g_lua_state;

void
initialize_scripting(void);

void
cleanup_lua_scripting(void);

void
begin_file(const char *filename);

void
end_file(void);

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

void
push_to_stack(const push_to_stack_info_t &info);

void
test_script(void);

using lua_function_t = int32_t (*)(lua_State *state);

template <typename T> void
add_global_to_lua(script_primitive_type_t type, const char *name, const T &data)
{
    const void *data_ptr = &data;

    switch(type)
    {
    case script_primitive_type_t::NUMBER: {int32_t *int_ptr = (int32_t *)data_ptr; lua_pushnumber(g_lua_state, *int_ptr); break;}
        //    case script_primitive_type_t::BOOLEAN: {bool *bool_ptr = (bool *)data_ptr; lua_pushboolean(g_lua_state, *bool_ptr); break;}
    case script_primitive_type_t::STRING: {const char **str_ptr = (const char **)data_ptr; lua_pushstring(g_lua_state, *str_ptr); break;}
    case script_primitive_type_t::FUNCTION: {lua_function_t *f_ptr = (lua_function_t *)data_ptr; lua_pushcfunction(g_lua_state, *f_ptr); break;}
    }

    lua_setglobal(g_lua_state, name);
}

void
execute_lua(const char *code);
