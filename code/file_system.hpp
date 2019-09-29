#pragma once

#include "core.hpp"
#include <stdint.h>

typedef int32_t file_handle_t;

enum { INVALID_FILE_HANDLE = -1 };

enum file_type_t { TEXT, BINARY, IMAGE };
file_handle_t create_file(const char *file, file_type_t type);

bool has_file_changed(file_handle_t handle);

struct file_contents_t
{
    uint32_t size;
    byte_t *content;
};

file_contents_t read_file_tmp(file_handle_t handle);

struct external_image_data_t
{
    int32_t width;
    int32_t height;
    void *pixels;
    int32_t channels;
};

external_image_data_t read_image_tmp(file_handle_t handle);
