#pragma once

#include "utils.hpp"
#include "vulkan.hpp"

extern "C"
{
    #include <lua.h>
    #include <lauxlib.h>
    #include <lualib.h>
}

extern lua_State *g_lua_state;

void
make_lua_scripting(void);

void
cleanup_lua_scripting(void);

void
begin_file(const char *filename);

void
end_file(void);

enum Script_Primitive_Type { NUMBER, STRING, BOOLEAN, FUNCTION, TABLE_INDEX, TABLE_HANDLE, TABLE_FIELD };
enum Stack_Item_Type { GLOBAL, FIELD, ARRAY_INDEX };

struct Push_To_Stack_Info
{
    Stack_Item_Type type;

    const char *name;

    /* Optional */
    s32 array_index {0};
    s32 stack_index = {0};
};

void
push_to_stack(const Push_To_Stack_Info &info);

void
test_script(void);

using Lua_Function = s32 (*)(lua_State *state);

template <typename T> void
add_global_to_lua(Script_Primitive_Type type, const char *name, const T &data)
{
    const void *data_ptr = &data;

    switch(type)
    {
    case Script_Primitive_Type::NUMBER: {s32 *int_ptr = (s32 *)data_ptr; lua_pushnumber(g_lua_state, *int_ptr); break;}
    case Script_Primitive_Type::BOOLEAN: {bool *bool_ptr = (bool *)data_ptr; lua_pushboolean(g_lua_state, *bool_ptr); break;}
    case Script_Primitive_Type::STRING: {const char **str_ptr = (const char **)data_ptr; lua_pushstring(g_lua_state, *str_ptr); break;}
    case Script_Primitive_Type::FUNCTION: {Lua_Function *f_ptr = (Lua_Function *)data_ptr; lua_pushcfunction(g_lua_state, *f_ptr); break;}
    }

    lua_setglobal(g_lua_state, name);
}
