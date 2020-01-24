#include "fonts.hpp"


static constexpr uint32_t MAX_FONTS = 5;
static font_t fonts[MAX_FONTS] = {};
static uint32_t font_count;
static hash_table_inline_t<uint32_t, 10, 3, 4> font_map;



static char *fnt_skip_break_characters(char *pointer);
static char *fnt_goto_next_break_character(char *pointer);
static char *fnt_skip_line(char *pointer);
static char *fnt_move_and_get_word(char *pointer, struct fnt_word_t *dst_word);
static char *fnt_skip_until_digit(char *pointer);
static int32_t fnt_atoi(fnt_word_t word);
static char *fnt_get_char_count(char *pointer, int32_t *count);
static char *fnt_get_font_attribute_value(char *pointer, int32_t *value);


font_handle_t add_font(const constant_string_t &font_name)
{
    uint32_t current_count = font_count++;
    font_map.insert(font_name.hash, current_count);
    return(current_count);
}


font_t *get_font(const constant_string_t &name)
{
    return(&fonts[*font_map.get(name.hash)]);
}


font_t *load_font(const constant_string_t &font_name, const char *fnt_file, const char *png_file)
{
    // TODO : Make sure this is parameterised, not fixed!
    static constexpr float32_t FNT_MAP_W = 512.0f, FNT_MAP_H = 512.0f;
    
    font_handle_t hdl = add_font(font_name);
    font_t *font_ptr = get_font(font_name);

    file_handle_t fnt_file_handle = create_file(fnt_file, file_type_flags_t::TEXT | file_type_flags_t::ASSET);
    file_contents_t fnt = read_file_tmp(fnt_file_handle);
    int32_t char_count = 0;
    char *current_char = fnt_get_char_count((char *)fnt.content, &char_count);
    // Ready to start parsing the file
    for (uint32_t i = 0; i < char_count; ++i)
    {
        // Char ID value
        int32_t char_id = 0;
        current_char = fnt_get_font_attribute_value(current_char, &char_id);

        // X value
        int32_t x = 0;
        current_char = fnt_get_font_attribute_value(current_char, &x);

        // X value
        int32_t y = 0;
        current_char = fnt_get_font_attribute_value(current_char, &y);
        y = FNT_MAP_H - y;
        
        // Width value
        int32_t width = 0;
        current_char = fnt_get_font_attribute_value(current_char, &width);

        // Height value
        int32_t height = 0;
        current_char = fnt_get_font_attribute_value(current_char, &height);

        // XOffset value
        int32_t xoffset = 0;
        current_char = fnt_get_font_attribute_value(current_char, &xoffset);

        // YOffset value
        int32_t yoffset = 0;
        current_char = fnt_get_font_attribute_value(current_char, &yoffset);

        // XAdvanc value
        int32_t xadvance = 0;
        current_char = fnt_get_font_attribute_value(current_char, &xadvance);

        font_character_t *character = &font_ptr->font_characters[char_id];
        character->character_value = (char)char_id;
        // ----------------------------------------------------------------------------- \/ Do y - height so that base position is at bottom of character
        character->uvs_base = vector2_t((float32_t)x / (float32_t)FNT_MAP_W, (float32_t)(y - height) / (float32_t)FNT_MAP_H);
        character->uvs_size = vector2_t((float32_t)width / (float32_t)FNT_MAP_W, (float32_t)height / (float32_t)FNT_MAP_H);
        character->display_size = vector2_t((float32_t)width / (float32_t)xadvance, (float32_t)height / (float32_t)xadvance);
        character->offset = vector2_t((float32_t)xoffset / (float32_t)xadvance, (float32_t)yoffset / (float32_t)xadvance);
        character->offset.y *= -1.0f;
        character->advance = (float32_t)xadvance / (float32_t)xadvance;
        
        current_char = fnt_skip_line(current_char);
    }

    remove_and_destroy_file(fnt_file_handle);

    return(font_ptr);
}



void ui_text_t::initialize(ui_box_t *box,
                    font_t *in_font,
                    font_stream_box_relative_to_t in_relative_to,
                    float32_t px_x_start,
                    float32_t px_y_start,
                    uint32_t in_chars_per_line,
                    float32_t in_line_height)
{
    this->dst_box = box;
    this->font = in_font;
    this->relative_to = in_relative_to;
    this->x_start = px_x_start;
    this->y_start = px_y_start;
    this->chars_per_line = in_chars_per_line;
    this->line_height = in_line_height;
}


void ui_text_t::draw_char(char character, uint32_t color)
{
    colors[char_count] = color;
    characters[char_count++] = character;
}


void ui_text_t::draw_string(const char *string, uint32_t color)
{
    uint32_t string_length = strlen(string);
    memcpy(characters + char_count, string, sizeof(char) * string_length);
    for (uint32_t i = 0; i < string_length; ++i)
    {
        colors[char_count + i] = color;
    }
    char_count += string_length;
}


void ui_text_t::null_terminate(void)
{
    characters[char_count] = 0;
}


void ui_input_text_t::input(raw_input_t *raw_input)
{
    for (uint32_t i = 0; i < raw_input->char_count; ++i)
    {
        char character[2] = {raw_input->char_stack[i], 0};
        if (character[0])
        {
            text.colors[cursor_position] = text_color;
            text.characters[cursor_position++] = raw_input->char_stack[i];
            ++text.char_count;
            raw_input->char_stack[i] = 0;
        }
    }

    if (raw_input->buttons[button_type_t::BACKSPACE].state)
    {
        if (text.char_count && cursor_position)
        {
            --cursor_position;
            --text.char_count;
        }
    }
}


// Loading fonts
struct fnt_word_t
{
    char *pointer;
    uint16_t size;
};

// Except new line
static char *fnt_skip_break_characters(char *pointer)
{
    for(;;)
    {
        switch(*pointer)
        {
        case ' ': case '=': ++pointer;
        default: return pointer;
        }
    }
    return nullptr;
}

static char *fnt_goto_next_break_character(char *pointer)
{
    for(;;)
    {
        switch(*pointer)
        {
        case ' ': case '=': case '\n': return pointer;
        default: ++pointer;
        }
    }
    return nullptr;
}

static char *fnt_skip_line(char *pointer)
{
    for(;;)
    {
        switch(*pointer)
        {
        case '\n': return ++pointer;
        default: ++pointer;
        }
    }
    return nullptr;
}

static char *fnt_move_and_get_word(char *pointer, fnt_word_t *dst_word)
{
    char *new_pointer = fnt_goto_next_break_character(pointer);
    if (dst_word)
    {
        dst_word->pointer = pointer;
        dst_word->size = new_pointer - pointer;
    }
    return(new_pointer);
}

static char *fnt_skip_until_digit(char *pointer)
{
    for(;;)
    {
        switch(*pointer)
        {
        case '-': case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9': return pointer;
        default: ++pointer;
        }
    }
    return nullptr;
}

struct fnt_string_number_t
{
    fnt_word_t word;
    // Maximum 10 chars for a number
    char characters[10];
};

static int32_t fnt_atoi(fnt_word_t word)
{
    fnt_string_number_t number;
    memcpy(number.characters, word.pointer, sizeof(char) * word.size);
    number.characters[word.size] = '\0';
    return(atoi(number.characters));
}

static char *fnt_get_char_count(char *pointer, int32_t *count)
{
    for(;;)
    {
        fnt_word_t chars_str;
        pointer = fnt_move_and_get_word(pointer, &chars_str);
        if (chars_str.size == strlen("chars"))
        {
            pointer = fnt_skip_until_digit(pointer);
            fnt_word_t count_str;
            pointer = fnt_move_and_get_word(pointer, &count_str);
            *count = fnt_atoi(count_str);
            pointer = fnt_skip_line(pointer);
            return pointer;
        }
        pointer = fnt_skip_line(pointer);
    }
}

char *fnt_get_font_attribute_value(char *pointer, int32_t *value)
{
    pointer = fnt_skip_until_digit(pointer);
    fnt_word_t value_str;
    pointer = fnt_move_and_get_word(pointer, &value_str);
    *value = fnt_atoi(value_str);
    return(pointer);
}
