#pragma once

#include <arch/Platform.h>
#include <Atomic.h>

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

        constexpr PmRegion(uintptr_t base, size_t length, uint8_t* bitmap)
        : base(base), totalPages(length / PageSize), freePages(length / PageSize), next(nullptr),
        bitmapHint(0), bitmap(bitmap), lock()
        {}
    };

    struct PmZone
    {
        PmRegion* head;
        PmRegion* tail;
        sl::Atomic<size_t> total;
        sl::Atomic<size_t> totalUsed;

        constexpr PmZone() : head(nullptr), tail(nullptr), total(0), totalUsed(0)
        {}
    };
    
    class PhysicalMemoryManager
    {
    private:
        PmRegion* regionAlloc;
        size_t remainingRegionAllocs;
        uint8_t* bitmapAlloc;
        size_t bitmapFreeSize;

        PmZone zones[2];

        void InsertRegion(PmZone& zone, uintptr_t base, size_t length);
        uintptr_t RegionAlloc(PmRegion& region, size_t count);

    public:
        constexpr PhysicalMemoryManager() : regionAlloc(nullptr), remainingRegionAllocs(0),
        bitmapAlloc(nullptr), bitmapFreeSize(0), zones()
        {}

        static PhysicalMemoryManager& Global();

        PhysicalMemoryManager(const PhysicalMemoryManager&) = delete;
        PhysicalMemoryManager& operator=(const PhysicalMemoryManager&) = delete;
        PhysicalMemoryManager(PhysicalMemoryManager&&) = delete;
        PhysicalMemoryManager& operator=(PhysicalMemoryManager&&) = delete;

        void Init();
        void IngestMemory(uintptr_t base, size_t length);

        //allocates ONLY within the 32-bit physical address space (low zone).
        uintptr_t AllocLow(size_t count = 1);
        //prefers to allocate above 32-bit address space, but will allocate below if needed.
        uintptr_t Alloc(size_t count = 1);
        void Free(uintptr_t base, size_t count = 1);
    };
}

using PMM = Npk::Memory::PhysicalMemoryManager;
