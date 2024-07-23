#pragma once

#include <Locks.h>
#include <Atomic.h>
#include <Random.h>

namespace Npk { struct MemmapEntry; } //defined in interfaces/loader/Generic.h

namespace Npk::Memory
{
    enum PmFlags : uintptr_t
    {
        Used = 1 << 0,
        Busy = 1 << 1,
    };

    struct PageInfo
    {
        sl::Atomic<PmFlags> flags;
        uintptr_t link;
    };

    struct PmContigZone
    {
        sl::InterruptLock lock;
        uint8_t* bitmap;
        uintptr_t base;
        size_t count;

        PmContigZone* next;
    };

    struct PmInfoSegment
    {
        PmInfoSegment* next;
        uintptr_t base;
        size_t length;
        PageInfo* info;
    };

    struct PmFreeEntry
    {
        PmFreeEntry* next;
        size_t runLength;
    };

    class PhysicalMemoryManager
    {
    private:
        sl::RwLock zonesLock;
        sl::RwLock segmentsLock;
        PmContigZone* zones;
        PmInfoSegment* segments;
        struct
        {
            sl::InterruptLock lock;
            PmFreeEntry* head;
        } freelist;

        sl::XoshiroRng rng;
        bool trashAfterUse;
        bool trashBeforeUse;

        void IngestMemory(sl::Span<MemmapEntry> entries, size_t contiguousQuota);
        void* AllocMeta(size_t size);

    public:
        PhysicalMemoryManager() = default;
        PhysicalMemoryManager(const PhysicalMemoryManager&) = delete;
        PhysicalMemoryManager& operator=(const PhysicalMemoryManager&) = delete;
        PhysicalMemoryManager(PhysicalMemoryManager&&) = delete;
        PhysicalMemoryManager& operator=(PhysicalMemoryManager&&) = delete;

        static PhysicalMemoryManager& Global();

        void Init();
        void ReclaimBootMemory();
        PageInfo* Lookup(uintptr_t physAddr);

        uintptr_t Alloc(size_t count = 1);
        void Free(uintptr_t base, size_t count = 1);
    };
}

using PMM = Npk::Memory::PhysicalMemoryManager;
