#pragma once

#include <NativePtr.h>
#include <boot/Stivale2.h>

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

    class PhysicalMemoryAllocator
    {
    private:
        sl::NativePtr allocBuffer;
        size_t allocBufferSize;
        PhysMemoryRegion* rootRegion;

        PhysMemoryRegion* InitRegion(stivale2_mmap_entry* mmapEntry);

    public:
        static PhysicalMemoryAllocator* Global();

        void Init(stivale2_struct_tag_memmap* mmap);
        void InitLate(); //called after virtual memory is set up

        void LockPage(sl::NativePtr address);
        void LockPages(sl::NativePtr lowestAddress, size_t count);
        void UnlockPage(sl::NativePtr address);
        void UnlockPages(sl::NativePtr lowestAddress, size_t count);

        void* AllocPage();
    };

    using PMM = PhysicalMemoryAllocator;
}
