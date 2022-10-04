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
        PMRegion* head;
        PMRegion* tail;


        void AppendRegion(uintptr_t baseAddr, size_t sizeBytes);

    public:
        static PhysicalMemoryManager& Global();

        void Init();
        PhysicalMemoryManager() = default;
        PhysicalMemoryManager(const PhysicalMemoryManager&) = delete;
        PhysicalMemoryManager& operator=(const PhysicalMemoryManager&) = delete;
        PhysicalMemoryManager(PhysicalMemoryManager&&) = delete;
        PhysicalMemoryManager& operator=(PhysicalMemoryManager&&) = delete;

        void* Alloc(size_t count = 1);
        void Free(void* base, size_t count = 1);
    };
}

using PMM = Npk::Memory::PhysicalMemoryManager;
