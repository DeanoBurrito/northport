#pragma once

#include <stdint.h>
#include <stddef.h>
#include <Locks.h>

namespace Npk::Memory
{
    struct SlabSegment
    {
        size_t totalCount;
        size_t freeCount;
        uint8_t* bitmap;
        uintptr_t allocBase;
        size_t hint;
        SlabSegment* next;

        SlabSegment(uintptr_t base, uint8_t* bitmap, size_t count)
        : totalCount(count), freeCount(count), bitmap(bitmap), allocBase(base), hint(0), next(nullptr)
        {}
    };

    class SlabAlloc
    {
    private:
        SlabSegment* head;
        SlabSegment* tail;
        size_t slabSize;
        size_t slabsPerSeg;
        sl::TicketLock lock;

        SlabSegment* InitSegment(uintptr_t base);

    public:
        void Init(uintptr_t firstSegment, size_t sizeSize, size_t slabCount);

        void* Alloc(uintptr_t& expansionBase);
        bool Free(void* ptr);
    };
}
