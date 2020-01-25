#pragma once

#include "core.hpp"
#include <stdint.h>

typedef int32_t file_handle_t;

enum { INVALID_FILE_HANDLE = -1 };

enum file_type_flags_t : uint32_t { TEXT, BINARY = 1 << 1, IMAGE = 1 << 2, ASSET = 1 << 3 };
typedef uint32_t file_type_t;
file_handle_t create_file(const char *file, file_type_t type);
file_handle_t create_writeable_file(const char *file, file_type_t type);
void remove_and_destroy_file(file_handle_t handle);

bool has_file_changed(file_handle_t handle);

struct file_contents_t
{
    uint32_t size;
    byte_t *content;
};

file_contents_t read_file_tmp(file_handle_t handle);
file_contents_t read_file(file_handle_t handle);

void write_file(file_handle_t, byte_t *bytes, uint32_t size);

struct external_image_data_t
{
    int32_t width;
    int32_t height;
    void *pixels;
    int32_t channels;
};

external_image_data_t read_image(file_handle_t handle);
void free_external_image_data(external_image_data_t *data);
