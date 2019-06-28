#pragma once

#include "utils.hpp"

void
make_lua_scripting(void);

void
cleanup_lua_scripting(void);

void
begin_file(const char *filename);

void
end_file(void);

enum Script_Primitive_Type { NUMBER, STRING, BOOLEAN, TABLE_INDEX, TABLE_HANDLE, TABLE_FIELD };
enum Stack_Item_Type { GLOBAL, FIELD };

struct Stack_Frame_Ptr
{
    s32 ptr;
    Stack_Frame_Ptr *parent;

    s32
    get_index(u32 inside_count);    
};

Stack_Frame_Ptr
begin_stack_frame(Stack_Frame_Ptr *parent = nullptr);

void
push_to_stack(Stack_Item_Type type, const char *name, Stack_Frame_Ptr *from);

void
test_script(void);
