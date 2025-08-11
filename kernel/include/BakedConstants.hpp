#pragma once

#include <Types.hpp>

namespace Npk
{
    extern const char* gitHash;
    extern const bool gitDirty;
    extern const char* compileFlags;
    extern const size_t versionMajor;
    extern const size_t versionMinor;
    extern const size_t versionRev;
}

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
    extern char KERNEL_CPULOCALS_BEGIN[];
    extern char KERNEL_CPULOCALS_END[];

    extern char* BspStackTop;
}
