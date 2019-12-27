#include "utils.hpp"
#include "script.hpp"
#include "ui.hpp"

extern "C"
{
    #include <lua.h>
    #include <lauxlib.h>
    #include <lualib.h>
}


// Global
static lua_State *lua_state = nullptr;



void initialize_scripting(void)
{
    lua_state = luaL_newstate();

    luaL_openlibs(lua_state);
}


void destroy_lua_scripting(void)
{
    lua_close(lua_state);
}    


void begin_file(const char *filename)
{
    // Cleanup previous state that is on the stack from the previous file    
    luaL_dofile(lua_state, filename);
}


void end_file(void)
{
    lua_settop(lua_state, 0);
}


void push_to_stack(const char *name, stack_item_type_t type, int32_t stack_index = 0, int32_t array_index = 0)
{
    switch(type)
    {
    case stack_item_type_t::GLOBAL:      {lua_getglobal(lua_state, name); break;}
    case stack_item_type_t::FIELD:       {lua_getfield(lua_state, stack_index, name); break;}
    case stack_item_type_t::ARRAY_INDEX: {lua_rawgeti(lua_state, stack_index, array_index); break;}
    }
}


void add_global_function(const char *name, lua_function_t function)
{
    lua_pushcfunction(lua_state, &function);
    lua_setglobal(lua_state, name);
}


void add_global_number(const char *name, int32_t number)
{
    lua_pushnumber(lua_state, &number);
    lua_setglobal(lua_state, name);
}


void add_global_string(const char *name, const char *string)
{
    lua_pushstring(lua_state, string);
    lua_setglobal(lua_state, name);
}


template <typename T> static T *cast_ptr(void *ptr) {return((T *)ptr);}

void get_from_stack(script_primitive_type_t type, int32_t stack_index, void *dst)
{
    switch(type)
    {
    case script_primitive_type_t::NUMBER:  {*cast_ptr<int>(dst) = lua_tonumber(lua_state, stack_index); break;}
    case script_primitive_type_t::STRING:  {*cast_ptr<const char *>(dst) = lua_tostring(lua_state, stack_index); break;}
        //    case script_primitive_type_t::BOOLEAN: {*cast_ptr<bool>(dst) = lua_toboolean(lua_state, stack_index); break;}
    }
}


void execute_lua(const char *code)
{
    int error = luaL_loadbuffer(lua_state, code, strlen(code), code) || lua_pcall(lua_state, 0, 0, 0);

    if (error)
    {
        const char *error = lua_tostring(lua_state, -1);

        console_out_color_override(error, 0xff0000ff);

        lua_pop(lua_state, 1);
    }
}
