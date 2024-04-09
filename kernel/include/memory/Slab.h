#pragma once

#include <stdint.h>
#include <stddef.h>
#include <Locks.h>
#include <containers/List.h>

namespace Npk::Memory
{
    struct SlabEntry
    {
        SlabEntry* next;
    };

    struct SlabSegment
    {
        SlabSegment* next;

        sl::TicketLock lock;
        sl::IntrFwdList<SlabEntry> entries;
        size_t freeCount;
        uintptr_t base;
    };

    class SlabAlloc
    {
    private:
        sl::RwLock segmentsLock;
        sl::IntrFwdList<SlabSegment> segments;
        unsigned slabSize;
        unsigned slabsPerSeg;
        bool doBoundsCheck;

        SlabSegment* CreateSegment();
        void DestroySegment(SlabSegment* seg);

    public:
        void Init(size_t slabBytes, size_t slabCountPerSeg, bool checkBounds);

        [[nodiscard]]
        void* Alloc();
        [[nodiscard]]
        bool Free(void* ptr);

        void CheckBounds();

        [[gnu::always_inline]]
        inline size_t Size()
        { return slabSize; }
    };
}
