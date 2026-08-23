#pragma once

#include <Hardware.hpp>

/* Types and functions for use with the bootloader interface. These are only
 * available during the early-init phase of kernel startup (See BringUp.cpp).
 */
namespace Npk::Loader
{
    struct EfiDetails
    {
        Paddr systemTable;
        Paddr memmapBase;
        size_t memmapSize;
        size_t memmapDescSize;
        uint32_t memmapDescVersion;
    };

    /* This struct defines info critical for the kernel to initialize,
     * as well as config root pointers and some 'nice to know' info.
     */
    struct LoadState
    {
        /* The kernel expects the bootloader to provide a direct map, this
         * field contains the base address of the map. If 0, ram (usable
         * memory) is identity mapped.
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

        /* If valid contains the physical address of the efi system table,
         * and a description of the efi memory map.
         */
        sl::Opt<EfiDetails> efi;

        /* If valid, physical base address of a blob passed from the bootloader.
         */
        sl::Opt<Paddr> moduleBlob;

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

    struct Framebuffer
    {
        Paddr base;
        size_t width;
        size_t height;
        size_t pitch;
        size_t bpp;
        uint8_t rShift;
        uint8_t rBits;
        uint8_t gShift;
        uint8_t gBits;
        uint8_t bShift;
        uint8_t bBits;
    };

    /* This function is called exactly once during very early kernel init, while
     * the bootloader page map is still active. It should populate and return a
     * `LoadState` struct for the kernel to initialize from. Any field of the 
     * struct not wrapped in an `sl::Opt<T>` type **must** be filled out or
     * kernel init will abort.
     */
    LoadState GetEntryState();

    /* This function will convert memory map data from the bootloader's format
     * to our own. It ignores the first `offset` number of usable entries, then
     * copies the base and length values of each usable physical address range
     * into the next unused slot in `ranges`. These values are in bytes but must
     * be page aligned. This function never writes beyond `ranges.Size()`.
     * The return value is the number of usable ranges written to `ranges`, if
     * smaller than `ranges.Size()` there are likely more ranges after what was
     * written.
     */
    size_t GetUsableRanges(sl::Span<MemoryRange> ranges, size_t offset);

    /* Similar to `GetUsableRanges()`, but for framebuffers provided by the
     * boot protocol.
     */
    size_t GetFramebuffers(sl::Span<Framebuffer> fbs, size_t offset);
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
    NpkStatus TryEnableEfiRuntimeServices(const Loader::EfiDetails& details, 
        uintptr_t& virtBase);
    void InitPageAccessCache(size_t entries, uintptr_t slots);

    void HwSetMyLocals(uintptr_t where, CpuId softwareId);
    void HwInitEarly();
    uintptr_t HwInitBspMmu(InitState& state, size_t tempMapCount);
    void HwEarlyMap(InitState& state, Paddr paddr, uintptr_t vaddr, 
        MmuPermissions perms, MmuCacheMode cacheMode);

    /* Simlar to `HwEarlyMap()` but always maps the same `paddr`, used to place
     * a static poison value in ranges of memory. This is a separate function to
     * allow the hardware layer to optimize the implementation: e.g. on
     * page table based systems a single page table for each level can be used,
     * since the translation always resolves the same way. The mapping must be
     * readonly.
     */
    void HwEarlyMapPoison(InitState& state, Paddr paddr, uintptr_t vaddr,
        size_t length);

    /* Called to switch to the kernel's runtime map, and ditch the bootloader
     * provided map (if present). No further access to loader data happens
     * beyond this point.
     */
    void HwCompleteBspMmuInit();

    /* Returns the maximum number of available CPUs (including the BSP) in the
     * system. The kernel has its own address map active at this point so
     * the page access cache and direct map (if present) are accessible at this
     * stage.
     */
    size_t HwGetCpuCount();

    /* Hook for hardware layer init, called after per-cpu stores are allocated
     * but before APs are booted. This runs with the kernel runtime address map
     * active. The `virtBase` argument is a bump allocator for address space.
     */
    void HwInitFull(uintptr_t& virtBase);

    /* Boots all available APs (CPUs other than the BSP/boot cpu). The per-cpu
     * data ranges contain space for the number of cpus reported by
     * `HwGetCpuCount()`, but this function is allowed to boot less than that,
     * if errors occur during bootup. APs booted will initialize themselves and
     * their local data but are not allowed to touch shared data until
     * `HwReleaseAps()` is called.
     *
     * This function returns the number of APs it booted.
     */
    size_t HwBootAps(uintptr_t& virtBase, PerCpuData data);

    /* Companion function to `HwBootAps()`: this function allows booted APs to
     * access shared data and continue on (and finish) their init.
     */
    void HwReleaseAps();

    /* Hook for hardware layer to perform further init, this is called after 
     * the virtual memory subsystem is ready.
     */
    void HwLateInit();
    
    NpkStatus LoadInitProgram();
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
    extern void (*PREINIT_ARRAY_BEGIN[])();
    extern void (*PREINIT_ARRAY_END[])();
    extern char KERNEL_CPULOCALS_BEGIN[];
    extern char KERNEL_CPULOCALS_END[];
    extern char KERNEL_NODELOCALS_BEGIN[];
    extern char KERNEL_NODELOCALS_END[];

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
