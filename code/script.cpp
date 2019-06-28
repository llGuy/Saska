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

s32
Stack_Frame_Ptr::get_index(u32 inside_count)
{
    Stack_Frame_Ptr *parent_ptr = parent;

    u32 total = inside_count + ptr;
        
    for (; parent_ptr; parent_ptr = parent_ptr->parent)
    {
        total += parent_ptr->ptr;
    }

    u32 size = lua_gettop(g_lua_state);

    return(total - size);
}

global_var u32 g_stack_size_inside_frame = 0;

Stack_Frame_Ptr
begin_stack_frame(Stack_Frame_Ptr *parent)
{
    g_stack_size_inside_frame = 0;
    
    Stack_Frame_Ptr new_frame = { parent ? parent->ptr : 0, parent };
    return(new_frame);
}

void
push_to_stack(Stack_Item_Type type, const char *name, Stack_Frame_Ptr *from)
{
    switch(type)
    {
    case Stack_Item_Type::GLOBAL: {lua_getglobal(g_lua_state, name); break;}
    case Stack_Item_Type::FIELD:  {lua_getfield(g_lua_state, from->get_index(g_stack_size_inside_frame), name); break;}
    }

    ++g_stack_size_inside_frame;
}

void
test_script(void)
{
    begin_file("scripts/tests/tables_indexing_test.lua");

    Stack_Frame_Ptr fr_global = begin_stack_frame(nullptr);
    {
        push_to_stack(GLOBAL, "a_table", &fr_global);
        
        Stack_Frame_Ptr fr_struct = begin_stack_frame(&fr_global);
        {
            
        }
    }
}
