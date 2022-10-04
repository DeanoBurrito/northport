#pragma once

#include <stdint.h>
#include <stddef.h>
#include <Locks.h>

namespace Npk::Memory
{
    struct PMRegion
    {
        uintptr_t base;
        size_t totalPages;
        size_t freePages;
        PMRegion* next;

        size_t bitmapHint;
        uint8_t* bitmap;
        sl::TicketLock lock;
    };
    
    class PhysicalMemoryManager
    {
    private:
        uint8_t* metaBuffer;
        PMRegion* zoneLow;
        PMRegion* zoneHigh;

        PMRegion* AppendRegion(PMRegion* zoneTail, uintptr_t baseAddr, size_t sizeBytes);
        uintptr_t RegionAlloc(PMRegion& region, size_t count);

    public:
        static PhysicalMemoryManager& Global();

        void Init();
        PhysicalMemoryManager() = default;
        PhysicalMemoryManager(const PhysicalMemoryManager&) = delete;
        PhysicalMemoryManager& operator=(const PhysicalMemoryManager&) = delete;
        PhysicalMemoryManager(PhysicalMemoryManager&&) = delete;
        PhysicalMemoryManager& operator=(PhysicalMemoryManager&&) = delete;

        //allocates ONLY within the 32-bit physical address space (low zone).
        uintptr_t AllocLow(size_t count = 1);
        //prefers to allocate above 32-bit address space, but will allocate below if needed.
        uintptr_t Alloc(size_t count = 1);
        void Free(uintptr_t base, size_t count = 1);
    };
}

using PMM = Npk::Memory::PhysicalMemoryManager;
