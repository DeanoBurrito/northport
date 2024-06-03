#include <stdint.h>
#include <arch/m68k/BootInfo.h>
#include <boot/LinkerSyms.h>
#include <boot/LimineTags.h>
#include <NativePtr.h>
#include <Maths.h>
#include <NanoPrintf.h>

namespace Loader
{
    using namespace Npk;

    constexpr size_t PageSize = 0x1000;

    enum class PanicReason : size_t
    {
        NoMemoryTag = 1,
        NotEnoughMemory = 2,
        KernelReturned = 3,
    };

    void Panic(PanicReason)
    {
        //leave d0 alone, it should contain our error code
        asm("clr %d1; add #0xDEAD, %d1");
        asm("clr %d2; add #0xDEAD, %d2");
        asm("clr %d3; add #0xDEAD, %d3");
        while (true)
            asm("stop #0x2700");
        __builtin_unreachable();
    }

    sl::CNativePtr FindNextInfoTag(sl::CNativePtr begin, BootInfoType type)
    {
        constexpr size_t ReasonableSearchCount = 50;

        if (begin.ptr == nullptr)
            begin = sl::AlignUp((uintptr_t)KERNEL_BLOB_BEGIN + (uintptr_t)KERNEL_BLOB_SIZE, 2);

        for (size_t i = 0; i < ReasonableSearchCount; i++)
        {
            auto tag = begin.As<BootInfoTag>();
            if (tag->type == BootInfoType::Last)
                return nullptr;
            if (tag->type == type)
                return begin;
            begin = begin.Offset(tag->size);
        }

        return nullptr;
    }

    limine_memmap_entry* mmapEntries;
    size_t mmapCount;
    size_t usedMmapCount;
    void SynthesizeMemoryMap()
    {
        mmapCount = 0;
        sl::CNativePtr chunkScan = nullptr;
        BootInfoMemChunk bigChunk {};

        //figure out number of memmap entries
        while (true)
        {
            chunkScan = FindNextInfoTag(chunkScan, BootInfoType::MemChunk);
            if (chunkScan.ptr == nullptr)
                break;
            mmapCount++;
            
            auto tag = chunkScan.As<BootInfoTag>();
            auto chunkInfo = chunkScan.Offset(sizeof(BootInfoTag)).As<BootInfoMemChunk>();
            if (chunkInfo->size > bigChunk.size)
                bigChunk = *chunkInfo;
            chunkScan = chunkScan.Offset(chunkScan.As<BootInfoTag>()->size);
        }
        if (mmapCount == 0)
            Panic(PanicReason::NoMemoryTag);

        mmapCount *= 2; //each usable region has an accompanying reclaimable region, for loader allocations
        //entries for kernel, initdisk/modules, null page protector, each gets 3 entries (2 for split, 1 for itself)
        mmapCount += 3 * 3; 

        //allocate space for memory map
        const size_t mmapSize = sl::AlignUp(mmapCount * sizeof(limine_memmap_entry), PageSize);
        if (mmapSize >= bigChunk.size)
            Panic(PanicReason::NotEnoughMemory);
        mmapEntries = reinterpret_cast<limine_memmap_entry*>(bigChunk.addr);

        //populate memory map
        usedMmapCount = 0;
        auto& nullPageEntry = mmapEntries[usedMmapCount++];
        nullPageEntry.type = LIMINE_MEMMAP_RESERVED;
        nullPageEntry.base = 0;
        nullPageEntry.length = PageSize;

        while (true)
        {
            chunkScan = FindNextInfoTag(chunkScan, BootInfoType::MemChunk);
            if (chunkScan.ptr == nullptr)
                break;

            auto tag = chunkScan.As<BootInfoTag>();
            auto chunkInfo = chunkScan.Offset(sizeof(BootInfoTag)).As<BootInfoMemChunk>();
            chunkScan = chunkScan.Offset(chunkScan.As<BootInfoTag>()->size);

            auto& loaderEntry = mmapEntries[usedMmapCount++];
            loaderEntry.type = LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE;
            loaderEntry.base = chunkInfo->addr;
            loaderEntry.length = 0;

            auto& entry = mmapEntries[usedMmapCount++];
            entry.type = LIMINE_MEMMAP_USABLE;
            entry.base = chunkInfo->addr;
            entry.length = chunkInfo->size;

            if (entry.base == bigChunk.addr)
            {
                const uintptr_t top = entry.base + entry.length;
                entry.base += mmapSize;
                entry.length = top - entry.base;
                loaderEntry.length = mmapSize;
            }
        }

        auto& kernelEntry = mmapEntries[usedMmapCount++];
        kernelEntry.type = LIMINE_MEMMAP_KERNEL_AND_MODULES;
        kernelEntry.base = reinterpret_cast<uint64_t>(KERNEL_BLOB_BEGIN);
        kernelEntry.length = reinterpret_cast<uint64_t>(KERNEL_BLOB_SIZE);

        if (auto initdiskTag = FindNextInfoTag(nullptr, BootInfoType::RamDisk); initdiskTag.ptr)
        {
            auto where = initdiskTag.Offset(sizeof(BootInfoTag)).As<BootInfoMemChunk>();
            auto& initdiskEntry = mmapEntries[usedMmapCount++];
            initdiskEntry.type = LIMINE_MEMMAP_KERNEL_AND_MODULES;
            initdiskEntry.base = where->addr;
            initdiskEntry.length = where->size;
        }
    }

    void SanitizeMemoryMap()
    {
        //whoever created the memory map could have been crazy, fix it up.

        for (size_t i = 0; i < usedMmapCount; i++)
        {
            auto& entry = mmapEntries[i];
            if (entry.type != LIMINE_MEMMAP_USABLE 
                && entry.type != LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE)
                continue;
        }
    }

    void* LoaderMalloc(size_t size)
    { return nullptr; }

    void SetupMmu()
    {}

    void FinalizeMemoryMap()
    {} //cleanup memory map
}

extern "C"
{
    extern void KernelEntry(); //defined in Init.cpp

    void ShimEntryNext()
    {
        using namespace Loader;

        Panic(PanicReason::KernelReturned);
        SynthesizeMemoryMap();
        SanitizeMemoryMap();
        SetupMmu();

        FinalizeMemoryMap();

        KernelEntry();
        Panic(PanicReason::KernelReturned);
    }
}
