#include "file_system.hpp"
#include <string.h>
#include <stb_image.h>

// by default is emptyr
global_var const char *virtual_file_path = "";
global_var Free_List_Allocator file_proc_allocator;

global_var Hash_Table_Inline<const char *, 10, 3, 2> mounted_virtual_paths("virtual_file_path_hash_table");

void
mount_virtual_file_path(const char *real_path, const char *virtual_path)
{
    // TODO(luc) : function to mount virtual file paths into the hash table
}

internal File_Data
read_binary(const File &file_description)
{
    FILE *file = fopen(file_description.file_path, "rb");
    if (file == nullptr)
    {
	OUTPUT_DEBUG_LOG("error - couldnt load file \"%s\"\n", file_description.file_path);
	assert(false);
    }
    fseek(file, 0, SEEK_END);
    u32 size = ftell(file);
    rewind(file);

    void *buffer = allocate_free_list(size);
    fread(buffer, 1, size, file);
    
    fclose(file);

    return(File_Data{buffer, size, file_description});
}    

internal File_Data
read_image(const File &file_description)
{
    s32 w, h, channels;
    stbi_uc *pixels = stbi_load(file_description.file_path, &w, &h, &channels, STBI_rgb_alpha);

    if (!pixels)
    {
	OUTPUT_DEBUG_LOG("error - couldnt load image \"%s\"\n", file_description.file_path);
	assert(false);
    }
    
    File_Data data;
    data.data = pixels;
    data.size = w * h * channels;
    data.extras[File_Data::Extra_Data::WIDTH] = w;
    data.extras[File_Data::Extra_Data::HEIGHT] = h;
    data.extras[File_Data::Extra_Data::CHANNELS] = channels;
    data.description = file_description;
    
    return(data);
}

File_Data
read_file_data(const File &file, Read_Flags flags)
{
    switch(file.format)
    {
    case File_Format::BINARY: return(read_binary(file));
    case File_Format::PNG: case File_Format::JPG: return(read_image(file));
    };

    return(File_Data{});
}

void
destroy_file_data(File_Data *file_data)
{
    switch(file_data->description.format)
    {
    case File_Format::BINARY:
	{
	    deallocate_free_list(file_data->data); return;
	}
    case File_Format::PNG: case File_Format::JPG:
	{
	    stbi_image_free(file_data->data);
	}
    }
}
