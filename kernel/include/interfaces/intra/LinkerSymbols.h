#pragma once

extern "C"
{
    extern char KERNEL_BLOB_BEGIN[];
    extern char KERNEL_BLOB_END[];
    extern char KERNEL_TEXT_BEGIN[];
    extern char KERNEL_TEXT_END[];
    extern char KERNEL_RODATA_BEGIN[];
    extern char KERNEL_RODATA_END[];
    extern char KERNEL_DATA_BEGIN[];
    extern char KERNEL_DATA_END[];

    extern void (*INIT_ARRAY_BEGIN[])();
    extern void (*INIT_ARRAY_END[])();
}
