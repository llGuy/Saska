#include "core.hpp"
#include "string.hpp"
#include "containers.hpp"
#include "file_system.hpp"
#include "variables.hpp"
#include "allocators.hpp"


enum variable_type_t { VT_INT, VT_STRING };


struct variable_t
{
    variable_type_t type;

    const char *start;

    void *data = nullptr;
};


static file_contents_t file_data;
static file_handle_t file;
static uint32_t variable_count = 0;
static variable_t variables[40] = {};
static hash_table_inline_t<uint32_t, 30, 3, 4> variable_key_map;


// Variables
static const char *user_name;


static int32_t get_int_value(variable_t *var)
{
    const char *value_start = var->start;

    for (; *value_start != ' '; ++value_start)
    {
    }

    ++value_start;
    
    uint32_t int_length = 0;
    for (; value_start[int_length] != '\n' && value_start[int_length] != '\r'; ++int_length)
    {
    }


    char *str_buffer = (char *)allocate_linear(int_length + 1);

    memcpy(str_buffer, value_start, int_length + 1);
    str_buffer[int_length] = 0;

    return atoi(str_buffer);
}

static const char *get_string_value(variable_t *var)
{
    const char *value_start = var->start;

    for (; *value_start != ' '; ++value_start)
    {
    }

    ++value_start;

    uint32_t string_length = 0;
    for (; value_start[string_length] != '\n' && value_start[string_length] != '\r'; ++string_length)
    {
    }

    char *str_buffer = (char *)allocate_free_list(string_length + 1);

    memcpy(str_buffer, value_start, string_length + 1);
    str_buffer[string_length] = 0;

    return str_buffer;
}


static void associate_address_with_variable(void *address, const char *var_name)
{
    variable_t *var = &variables[*variable_key_map.get(make_constant_string(var_name, strlen(var_name)).hash)];

    var->data = address;
}


static void fill_variables_with_values(void)
{
    for (uint32_t var_index = 0; var_index < variable_count; ++var_index)
    {
        variable_t *var = &variables[var_index];

        static auto is_number = [](const char *buffer) -> bool { return (buffer[0] >= 48 && buffer[0] <= 57); };

        // Determine type
        const char *value_start = var->start;

        for (; *value_start != ' '; ++value_start)
        {
        }
        ++value_start;

        if (is_number(value_start))
        {
            var->type = VT_INT;
        }
        else
        {
            var->type = VT_STRING;
        }
        
        switch(var->type)
        {
            
        case VT_INT: {
            int32_t *ptr = (int32_t *)var->data;

            *ptr = get_int_value(var);
        } break;

        case VT_STRING: {
            const char **ptr = (const char **)var->data;

            *ptr = get_string_value(var);
        } break;
            
        }
    }
}


void load_variables(void)
{
    file = create_writeable_file("game.variables", file_type_flags_t::ASSET);
    file_data = read_file(file);



    uint32_t stack_pointer = 0;
    const char *name_start;
    char current_variable_name_buffer[40] = {};

    bool go_to_next_line = 0;
    
    for (char *current = (char *)file_data.content; *current; ++current)
    {
        if (strcmp("end", current_variable_name_buffer) == 0)
        {
            break;
        }
        
        if (stack_pointer == 0)
        {
            name_start = current;
        }
        
        if (go_to_next_line)
        {
            if (*current == '\n')
            {
                go_to_next_line = 0;
            }
        }
        else if (*current == ' ')
        {
            current_variable_name_buffer[stack_pointer] = 0;

            uint32_t length = stack_pointer;
            
            stack_pointer = 0;
            
            go_to_next_line = 1;
            
            constant_string_t kstr = make_constant_string(current_variable_name_buffer, strlen(current_variable_name_buffer));
            

            variable_t var = { VT_INT, name_start };

            
            variable_key_map.insert(kstr.hash, variable_count);

            variables[variable_count++] = var;


            memset(current_variable_name_buffer, 0, length);
        }
        else
        {
            current_variable_name_buffer[stack_pointer++] = *current;
        }
    }


    associate_address_with_variable(&user_name, "user_name");

    fill_variables_with_values();
}

void save_variables(void)
{
    // Loop through all the variables
    char *buffer = (char *)allocate_linear(5000);

    uint32_t current_byte = 0;

    for (uint32_t var_index = 0; var_index < variable_count; ++var_index)
    {
        variable_t *var = &variables[var_index];

        // Append variable name
        const char *var_name = var->start;

        uint32_t var_name_length = 0;
        for (; *var_name != ' '; ++var_name)
        {
            buffer[current_byte + var_name_length] = *var_name;
            
            ++var_name_length;
        }

        buffer[current_byte + var_name_length] = ' ';

        current_byte += var_name_length + 1;

        // Push variable value
        switch (var->type)
        {
            
        case VT_INT: {
            int32_t *ptr = (int32_t *)var->data;

            char cbuffer[10] = {};

            itoa(*ptr, cbuffer, 10);

            uint32_t str_len = strlen(cbuffer);
            
            memcpy(&buffer[current_byte], cbuffer, str_len);

            current_byte += str_len;
        } break;
            
        case VT_STRING: {
            const char **ptr = (const char **)var->data;

            uint32_t str_len = strlen(*ptr);
            
            memcpy(&buffer[current_byte], *ptr, str_len);

            current_byte += str_len;
        } break;
            
        }

        buffer[current_byte++] = '\n';
    }

    buffer[current_byte++] = '\n';

    memcpy(buffer + current_byte, "end\0", 4 * sizeof(char));

    current_byte += 4;

    write_file(file, (byte_t *)buffer, current_byte);
}

// A bunch of variables
const char *&variables_get_user_name(void)
{
    return user_name;
}
