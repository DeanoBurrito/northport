#pragma once

#include <Platform.h>

namespace Kernel::Memory
{
    struct PhysMemoryRegion
    {
        sl::NativePtr baseAddress;
        size_t pageCount;
        size_t freePages;
        PhysMemoryRegion* next;
        char lock;

        size_t bitmapNextAlloc;
        uint8_t* bitmap;
    };

    struct PhysMemoryStats
    {
        constexpr static size_t pageSizeInBytes = PAGE_FRAME_SIZE;

        size_t totalPages;
        size_t usedPages;
        //the following 3 are not guarenteed to be overlapping, and are more suggestions.
        size_t reservedBytes;
        size_t kernelPages; //this also includes modules loaded with the kernel
        size_t reclaimablePages;

        PhysMemoryStats() = default;
        PhysMemoryStats(size_t total, size_t used) : totalPages(total), usedPages(used)
        {}
    };

    class PhysicalMemoryAllocator
    {
    private:
        sl::NativePtr allocBuffer;
        size_t allocBufferSize;
        PhysMemoryRegion* rootRegion;
        PhysMemoryStats stats;

        PhysMemoryRegion* InitRegion(void* mmapEntry);

        void LockPage(sl::NativePtr address);
        void LockPages(sl::NativePtr lowestAddress, size_t count);
        void UnlockPage(sl::NativePtr address);
        void UnlockPages(sl::NativePtr lowestAddress, size_t count);

    public:
        static PhysicalMemoryAllocator* Global();

        void InitFromLimine();

        void* AllocPage();
        void* AllocPages(size_t count);
        void FreePage(sl::NativePtr address);
        void FreePages(sl::NativePtr address, size_t count);

        [[gnu::always_inline]] inline 
        PhysMemoryStats GetStats() const
        { return stats; }
    };

    using PMM = PhysicalMemoryAllocator;
}
