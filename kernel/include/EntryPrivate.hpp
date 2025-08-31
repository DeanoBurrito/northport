#pragma once

#include <Types.hpp>
#include <Span.hpp>
#include <Optional.hpp>
#include <Time.hpp>

/* Types and functions for use with the bootloader interface. These are only
 * available during the early-init phase of kernel startup (See BringUp.cpp).
 */
namespace Npk::Loader
{
    struct LoadState
    {
        uintptr_t directMapBase;
        Paddr kernelBase;
        CpuId bspId;
        sl::Opt<Paddr> rsdp;
        sl::Opt<Paddr> fdt;
        sl::Opt<sl::TimePoint> timeOffset;
        sl::StringSpan commandLine;
    };

    struct MemoryRange
    {
        Paddr base;
        size_t length;
    };

    LoadState GetEntryState();
    size_t GetUsableRanges(sl::Span<MemoryRange> ranges, size_t offset);
}

/* These link to variables provided by a file that the build system
 * generates.
 */
namespace Npk
{
    extern const char* gitHash;
    extern const bool gitDirty;
    extern const char* compileFlags;
    extern const size_t versionMajor;
    extern const size_t versionMinor;
    extern const size_t versionRev;
}

/* The following symbols are defined either in the linker script
 * (any in ALL_CAPS[]), some are from the assembly entrypoint
 * in the APL (`BspStackTop`).
 */
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
