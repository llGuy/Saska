#include "utils.hpp"

#include "script.hpp"

extern "C"
{
    #include <lua.h>
    #include <lauxlib.h>
    #include <lualib.h>
}

lua_State *g_lua_state = nullptr;

void
make_lua_scripting(void)
{
    g_lua_state = luaL_newstate();

    luaL_openlibs(g_lua_state);
}

void
cleanup_lua_scripting(void)
{
    lua_close(g_lua_state);
}    

void
begin_file(const char *filename)
{
    // Cleanup previous state that is on the stack from the previous file    
    luaL_dofile(g_lua_state, filename);
}

void
end_file(void)
{
    lua_settop(g_lua_state, 0);
}

void
push_to_stack(const char *name, Stack_Item_Type type, int32_t stack_index = 0, int32_t array_index = 0)
{
    switch(type)
    {
    case Stack_Item_Type::GLOBAL:      {lua_getglobal(g_lua_state, name); break;}
    case Stack_Item_Type::FIELD:       {lua_getfield(g_lua_state, stack_index, name); break;}
    case Stack_Item_Type::ARRAY_INDEX: {lua_rawgeti(g_lua_state, stack_index, array_index); break;}
    }
}

template <typename T> T *cast_ptr(void *ptr) {return((T *)ptr);}

void
get_from_stack(Script_Primitive_Type type, int32_t stack_index, void *dst)
{
    switch(type)
    {
    case Script_Primitive_Type::NUMBER:  {*cast_ptr<int>(dst) = lua_tonumber(g_lua_state, stack_index); break;}
    case Script_Primitive_Type::STRING:  {*cast_ptr<const char *>(dst) = lua_tostring(g_lua_state, stack_index); break;}
    case Script_Primitive_Type::BOOLEAN: {*cast_ptr<bool>(dst) = lua_toboolean(g_lua_state, stack_index); break;}
    }
}

void
test_script(void)
{
    begin_file("scripts/tests/tables_indexing_test.lua");
    {
        push_to_stack("random_thing", GLOBAL);
        int result; 
        get_from_stack(NUMBER, -1, &result);

        add_global_to_lua(NUMBER, "foo", 42);
        
        printf("%d\n", result);
    }    
}
