#pragma once

#include <Optional.h>
#include <Span.h>

/* I want to make a point about this abstraction: the main reason the boot protocol is abstracted
 * from the kernel is because I intend to support 32-bit platforms, which the limine protocol
 * does not support idealogically. Of course, I've done this (see my original m68k port and it's
 * homebrewed limine protocol loader), but there are easier ways to go about it.
 * So while I'm not a big fan of this, I do think it's necessary for my use case. If you're here
 * because you're exploring the northport source code and hoping to learn something, this isn't it.
 */
namespace Npk
{
    struct MemmapEntry
    {
        uintptr_t base;
        size_t length;
    };

    struct LoaderFramebuffer
    {
        uintptr_t address;
        size_t width;
        size_t height;
        size_t stride;

        size_t pixelStride;
        uint8_t rShift;
        uint8_t gShift;
        uint8_t bShift;
        uint8_t rBits;
        uint8_t gBits;
        uint8_t bBits;
    };

    //early init for bootloader interface, print out lots of stuff, check responses make sense.
    void ValidateLoaderData();
    
    //returns if bootloader has provided a HHDM at all, and returns base + length if it has.
    bool GetHhdmBounds(uintptr_t& base, size_t& length);

    //returns the physical address the kernel is loaded at.
    uintptr_t GetKernelPhysAddr();

    //used in the initial stages of the kernel, before the pmm and wired heap are ready. It
    //allocates physical memory and modifies the memory map to keep track of allocations.
    //This memory can't be freed later on, so this function should be used sparingly.
    sl::Opt<uintptr_t> EarlyPmAlloc(size_t length);

    //populates an array of memmap entries for consumption. Returns the number of entries
    //populated (the rest are untouched), and offset allows the caller to select where to start
    //populating the array from. Ideally you call this in a loop, with the return value accumulated
    //and passed as the offset to the next iteration, until it returns less than entries.Size().
    size_t GetUsableMemmap(sl::Span<MemmapEntry> entries, size_t offset);

    //same as GetUsableMemmap, but for returns areas reclaimable by the kernel after
    //the global init sequence.
    size_t GetReclaimableMemmap(sl::Span<MemmapEntry> entries, size_t offset);

    //attempts to get the rsdp from the bootloader.
    sl::Opt<uintptr_t> GetRsdp();

    //attempts to get the FDT blob passed by the bootloader.
    sl::Opt<uintptr_t> GetDtb();

    //attempts to get the initrd passed by the bootloader.
    sl::Span<uint8_t> GetInitdisk();

    sl::Span<uint8_t> GetKernelSymbolTable();
    sl::Span<const char> GetKernelStringTable();

    //attempts to start other cores known by the bootloader and jump them to kernel code,
    //it also calls PerCoreEntry() for each core, including the BSP.
    size_t StartupAps();

    //returns the kernel command line provided by the bootloader. If no command line is
    //available, returns a stringspan of size 0.
    sl::StringSpan GetCommandLine();

    //functions similar to the GetMemmap() functions: the fbs array is filled data about
    //framebuffers known to the bootloader, and the amount of entries written is returned.
    //Offset can be used to fetch framebuffers beyond what was previously returned.
    //To get all framebuffers, this function should be called in a loop, until it returns <
    //fbs.Size().
    size_t GetFramebuffers(sl::Span<LoaderFramebuffer> fbs, size_t offset);
}
