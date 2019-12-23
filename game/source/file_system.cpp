#include <stb_image.h>
#include "file_system.hpp"
#include <string.h>
#include "memory.hpp"
#include "containers.hpp"
#include "allocators.hpp"
#include <Windows.h>

// For each platform, there will be something different
#ifdef _WIN32
struct file_object_t
{
    file_type_t file_type;
    const char *file_path;
    HANDLE handle;
    FILETIME last_write_time;
};

void initialize_file_handle(file_object_t *object)
{
    object->handle = CreateFileA(object->file_path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
}

void close_file(file_object_t *object)
{
    CloseHandle(object->handle);
}

FILETIME get_last_file_write(file_object_t *object)
{
    FILETIME creation, last_access, last_write;
    if (GetFileTime(object->handle, &creation, &last_access, &last_write))
    {
        return(last_write);
    }

    assert(0);

    return(last_write);
}

bool check_if_file_changed(file_object_t *object)
{
    FILETIME last_write_time = get_last_file_write(object);
    
    if (last_write_time.dwLowDateTime == object->last_write_time.dwLowDateTime && last_write_time.dwHighDateTime == object->last_write_time.dwHighDateTime)
    {
        // Didn't change
        return 0;
    }
    else
    {
        object->last_write_time = last_write_time;
        // Changed
        return 1;
    }
}

uint32_t get_file_size(file_object_t *object)
{
    LARGE_INTEGER size;
    GetFileSizeEx(object->handle, &size);
    return((uint32_t)size.QuadPart);
}

void read_from_file(file_object_t *object, byte_t *bytes, uint32_t buffer_size)
{
    DWORD bytes_read = 0;
    if (ReadFile(object->handle, bytes, buffer_size, &bytes_read, NULL) == FALSE)
    {
        assert(0);
    }
}

void initialize_filetime(file_object_t *object)
{
    object->last_write_time = get_last_file_write(object);
}
// #elif
#endif

#define MAX_FILES 50

static struct file_manager_t
{

    typedef stack_dynamic_container_t<file_object_t, 50> file_stack_dynamic_container_t;
    file_stack_dynamic_container_t files;

} g_files;

const char *create_asset_path(const char *file)
{
    const char *asset_base_path = "../assets/";
        
    uint32_t asset_base_path_length = strlen(asset_base_path);
    uint32_t file_path_length = strlen(file);
    uint32_t full_path_length = asset_base_path_length + file_path_length;
        
    char *full_path_buffer = (char *)allocate_free_list(full_path_length + 1);
    memcpy(full_path_buffer, asset_base_path, strlen(asset_base_path));
    memcpy(full_path_buffer + asset_base_path_length, file, file_path_length);
    full_path_buffer[full_path_length] = 0;

    return(full_path_buffer);
}

file_handle_t create_file(const char *file, file_type_t type)
{
    file_handle_t new_file = g_files.files.add();
    file_object_t *object = g_files.files.get(new_file);

    if (type & file_type_flags_t::ASSET)
    {
        object->file_path = create_asset_path(file);
    }
    else
    {
        object->file_path = file;
    }

    object->file_type = type;
    
    initialize_file_handle(object);
    initialize_filetime(object);

    return(new_file);
}

void remove_and_destroy_file(file_handle_t handle)
{
    file_object_t *object = g_files.files.get(handle);

    close_file(object);
    
    g_files.files.remove(handle);
}

bool has_file_changed(file_handle_t handle)
{
    file_object_t *object = g_files.files.get(handle);

    return(check_if_file_changed(object));
}

file_contents_t read_file_tmp(file_handle_t handle)
{
    file_object_t *object = g_files.files.get(handle);
    uint32_t size = get_file_size(object);
    byte_t *buffer = (byte_t *)allocate_linear(size);
    read_from_file(object, buffer, size);
    if (object->file_type & file_type_flags_t::TEXT)
    {
        buffer[size] = 0;
    }

    return file_contents_t{ size, buffer };
}

external_image_data_t read_image(file_handle_t handle)
{
    file_object_t *object = g_files.files.get(handle);
    external_image_data_t image_data = {};
    image_data.pixels = stbi_load(object->file_path, &image_data.width, &image_data.height, &image_data.channels, STBI_rgb_alpha);
    return image_data;
}

void free_external_image_data(external_image_data_t *data)
{
    stbi_image_free(data->pixels);
}
