#pragma once

#include <Hardware.hpp>

/* Types and functions for use with the bootloader interface. These are only
 * available during the early-init phase of kernel startup (See BringUp.cpp).
 */
namespace Npk::Loader
{
    /* This struct defines info critical for the kernel to initialize,
     * as well as config root pointers and some 'nice to know' info.
     */
    struct LoadState
    {
        /* The kernel expects the bootloader to provide a direct map, this
         * field contains the base address of the map. If 0, ram (usable
         * memory) is identiy mapped.
         * Only memory marked as 'usable' will be accessed via the direct map.
         */
        uintptr_t directMapBase;

        /* Physical base address of the kernel image. The kernel image must
         * be contiguous in physical memory.
         */
        Paddr kernelBase;

        /* Contains the **hardware** id for the BSP. On some platforms this
         * can be assumed zero, others it is not required to be and therefore
         * the bootloader must provide this info.
         */
        CpuId bspId;

        /* If valid, physical address of the RSDP.
         */
        sl::Opt<Paddr> rsdp;

        /* If valid, physical base address of an FDT describing the system.
         */
        sl::Opt<Paddr> fdt;

        /* If valid, physical address of the EFI system table.
         * NOTE: this is the system table, not the runtime services table.
         */
        sl::Opt<Paddr> efiTable;

        /* If valid, contains the offset of the alarm/system timer relative
         * to the unix epoch.
         */
        sl::Opt<sl::TimePoint> timeOffset;

        /* The kernel command line, as known by the bootloader.
         */
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

/* Types and functions related to kernel bringup.
 */
namespace Npk
{
    struct InitState
    {
        uintptr_t dmBase;
        size_t pmaCount;
        uintptr_t pmaSlots;

        uintptr_t vmAllocHead;
        Paddr pmAllocHead;
        size_t pmAllocIndex;
        size_t usedPages;

        sl::StringSpan mappedCmdLine;

        char* VmAlloc(size_t length);
        char* VmAllocAnon(size_t length);
        Paddr PmAlloc();
    };

    struct PerCpuData
    {
        uintptr_t localsBase;
        uintptr_t apStacksBase;
        size_t localsStride;
        size_t stackStride;
    };

    [[noreturn]]
    void EarlyPanic(sl::StringSpan why);

    void SetConfigRoot(const Loader::LoadState& loaderState);
    void TryMapAcpiTables(uintptr_t& virtBase);
    void TryEnableEfiRtServices(Paddr systemTable, uintptr_t& virtBase);
    void InitPageAccessCache(size_t entries, uintptr_t slots);

    void HwSetMyLocals(uintptr_t where, CpuId softwareId);
    void HwInitEarly();
    uintptr_t HwInitBspMmu(InitState& state, size_t tempMapCount);
    void HwEarlyMap(InitState& state, Paddr paddr, uintptr_t vaddr, 
        MmuFlags flags);
    size_t HwGetCpuCount();
    void ArchInitFull(uintptr_t& virtBase);
    void PlatInitFull(uintptr_t& virtBase);
    void HwBootAps(uintptr_t& virtBase, PerCpuData data);
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

#ifndef NPK_ASSERT_STRINGIFY
#define NPK_ASSERT_STRINGIFY(x) NPK_ASSERT_STRINGIFY2(x)
#endif

#ifndef NPK_ASSERT_STRINGIFY2
#define NPK_ASSERT_STRINGIFY2(x) #x
#endif

#define NPK_EARLY_ASSERT(cond) \
    if (SL_UNLIKELY(!(cond))) \
    { \
        Npk::EarlyPanic("Assert failed (" SL_FILENAME_MACRO ":" \
            NPK_ASSERT_STRINGIFY(__LINE__) "): " #cond); \
    }

#define NPK_EARLY_UNREACHABLE() \
    NPK_EARLY_ASSERT(!"UNreachable code reached."); \
    SL_UNREACHABLE();
