#pragma once

extern "C"
{
    [[gnu::visibility("default")]]
    extern char KERNEL_BLOB_BEGIN[];
    [[gnu::visibility("default")]]
    extern char KERNEL_BLOB_SIZE[];
    [[gnu::visibility("default")]]
    extern char KERNEL_TEXT_BEGIN[];
    [[gnu::visibility("default")]]
    extern char KERNEL_TEXT_SIZE[];
    [[gnu::visibility("default")]]
    extern char KERNEL_RODATA_BEGIN[];
    [[gnu::visibility("default")]]
    extern char KERNEL_RODATA_SIZE[];
    [[gnu::visibility("default")]]
    extern char KERNEL_DATA_BEGIN[];
    [[gnu::visibility("default")]]
    extern char KERNEL_DATA_SIZE[];
}
