#pragma once

#include <stdint.h>
#include <stddef.h>
#include <Locks.h>

namespace Npk::Memory
{
    struct PmRegion
    {
        uintptr_t base;
        size_t totalPages;
        size_t freePages;
        PmRegion* next;

        size_t bitmapHint;
        uint8_t* bitmap;
        sl::TicketLock lock;
    };
    
    class PhysicalMemoryManager
    {
    private:
        uint8_t* metaBuffer;
        size_t metaBufferSize;
        PmRegion* zoneLow;
        PmRegion* zoneHigh;
        PmRegion* lowTail;
        PmRegion* highTail;

        struct 
        {
            uint32_t totalLow;
            uint32_t totalHigh;
            uint32_t usedLow;
            uint32_t usedHigh;
        } counts; //these are in pages, not bytes.

        void InsertRegion(PmRegion** head, PmRegion** tail, uintptr_t base, size_t length);
        uintptr_t RegionAlloc(PmRegion& region, size_t count);

    public:
        static PhysicalMemoryManager& Global();

        PhysicalMemoryManager() = default;
        PhysicalMemoryManager(const PhysicalMemoryManager&) = delete;
        PhysicalMemoryManager& operator=(const PhysicalMemoryManager&) = delete;
        PhysicalMemoryManager(PhysicalMemoryManager&&) = delete;
        PhysicalMemoryManager& operator=(PhysicalMemoryManager&&) = delete;

        void Init();
        void IngestMemory(uintptr_t base, size_t length);
        void DumpState();

        //allocates ONLY within the 32-bit physical address space (low zone).
        uintptr_t AllocLow(size_t count = 1);
        //prefers to allocate above 32-bit address space, but will allocate below if needed.
        uintptr_t Alloc(size_t count = 1);
        void Free(uintptr_t base, size_t count = 1);
    };
}

using PMM = Npk::Memory::PhysicalMemoryManager;
