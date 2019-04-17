#pragma once

#include "core.hpp"

// create a file handling system : for example track certain files to see if they change and what to do if that is the case

enum File_Format
{
    BINARY
    , PNG
    , JPG
    , WAV
};

struct File
{
    const char *file_path;
    File_Format format;
};

struct File_Data
{
    void *data;
    u32 size;

    File description;

    // all types of extra data
    enum Extra_Data { WIDTH, HEIGHT, CHANNELS, INVALID };
    u32 extras[Extra_Data::INVALID];
};

void
mount_virtual_file_path(const char *real, const char *mask);

enum Read_Flags {RECORD = 1 << 0};

File_Data
read_file_data(const File &file, Read_Flags flags);

void
destroy_file_data(File_Data *file_data);

template <typename T> struct File_Proc_Wrapper
{
    T proc;

    FORCEINLINE void
    execute(void)
    {
	proc();
    }
};

template <typename T> void
attach_update_proc(T &&proc, const File &file)
{
    
}
