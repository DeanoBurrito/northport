#pragma once

#include <stdint.h>
#include <stddef.h>
#include <Locks.h>
#include <Span.h>

namespace Npk::Memory
{
    struct SlabSegment
    {
        size_t freeCount; //total number of free slabs
        uint8_t* bitmap;
        size_t hint; //hint for where to start next allocation
        uintptr_t allocBase;
        sl::TicketLock lock;
        SlabSegment* next;
    };

    class SlabAlloc
    {
    private:
        SlabSegment* head;
        size_t slabSize; //size of slab this instance deals with
        size_t segmentCapacity; //number of slabs per segment
        size_t segmentSize; //size of segment in bytes, including metadata

        SlabSegment* CreateSegment();
        void DestroySegment(SlabSegment* segment);

    public:
        SlabAlloc() = default;
        SlabAlloc(const SlabAlloc&) = delete;
        SlabAlloc& operator=(const SlabAlloc&) = delete;
        SlabAlloc(SlabAlloc&&) = delete;
        SlabAlloc& operator=(SlabAlloc&&) = delete;

        void Init(size_t slabSizeBytes, size_t segSize, size_t createCount);

        [[nodiscard]]
        void* Alloc();
        [[nodiscard]]
        bool Free(void* ptr);

        [[gnu::always_inline]]
        inline size_t Size()
        { return slabSize; }
    };
}
