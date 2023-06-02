#include <memory/Slab.h>
#include <debug/Log.h>
#include <memory/Vmm.h>
#include <Bitmap.h>
#include <Memory.h>

namespace Npk::Memory
{
    SlabSegment* SlabAlloc::CreateSegment()
    {
        const size_t bitmapBytes = sl::AlignUp(segmentCapacity, 8) / 8;
        const size_t reservedBytes = segmentSize - (segmentCapacity * slabSize);
        ASSERT(reservedBytes >= bitmapBytes + sizeof(SlabSegment), "huh");

        auto maybeRegion = VMM::Kernel().Alloc(segmentSize, 0, VmFlags::Write | VmFlags::Anon);
        VALIDATE(maybeRegion, nullptr, "Slab segment creation failed: VMM alloc failed.")
        const uintptr_t base = maybeRegion->base;

        uint8_t* bitmap = reinterpret_cast<uint8_t*>(base + sizeof(SlabSegment));
        sl::memset(bitmap, 0, bitmapBytes);

        SlabSegment* segment = new((void*)base) SlabSegment();
        segment->hint = 0;
        segment->freeCount = segmentCapacity;
        segment->bitmap = bitmap;
        segment->allocBase = base + reservedBytes;
        segment->next = nullptr;

        const size_t slackBytes = reservedBytes - (bitmapBytes + sizeof(SlabSegment));
        Log("KSlab segment created: %lux %lub (%lub reserved, %lub slack)", LogLevel::Verbose, 
            segmentCapacity, slabSize, reservedBytes - slackBytes, slackBytes);

        return segment;
    }

    void SlabAlloc::DestroySegment(SlabSegment* segment)
    {
        ASSERT_UNREACHABLE()
    }

    void SlabAlloc::Init(size_t slabSizeBytes, size_t segSize, size_t createCount)
    {
        slabSize = slabSizeBytes;
        segmentSize = segSize;
        const size_t bitmapSize = sl::AlignUp(segSize / slabSize, 8) / 8;
        const size_t metadataSize = bitmapSize + sizeof(SlabSegment);
        segmentCapacity = (segmentSize - metadataSize) / slabSize;

        SlabSegment* tail = nullptr;
        createCount = sl::Max(1ul, createCount); //list must contain at least 1 segment
        for (size_t i = 0; i < createCount; i++)
        {
            SlabSegment* latest = CreateSegment();
            ASSERT(latest != nullptr, "CreateSegment() failed during slab init.");

            if (tail != nullptr) //single list append
                tail->next = latest;
            else
                head = latest;
            tail = latest;
        }
    }

    void* SlabAlloc::Alloc()
    {
        SlabSegment* tail = nullptr;
        for (SlabSegment* seg = head; seg != nullptr; seg = seg->next)
        {
            tail = seg;
            seg->lock.Lock();
            if (seg->freeCount == 0)
            {
                if (seg->next != nullptr)
                    seg->lock.Unlock();
                continue;
            }
            
            const size_t found = sl::BitmapFindClear(seg->bitmap, segmentCapacity); //TODO: use hinting
            ASSERT(found != segmentCapacity, "Segment bitmap out of sync?");

            sl::BitmapSet(seg->bitmap, found);
            seg->freeCount--;
            seg->hint = (found + 1) % segmentCapacity; //TODO: sort based on fullness
            seg->lock.Unlock();

            return reinterpret_cast<void*>(seg->allocBase + (found * slabSize));
        }
        
        //all segments are in use: we still have the tail segment locked, so append
        //and allocate directly from the new segment.
        SlabSegment* latest = CreateSegment();
        ASSERT(latest != nullptr, "CreateSegment() failed during expansion.");

        latest->lock.Lock();
        sl::BitmapSet(latest->bitmap, 0);
        latest->freeCount--;
        latest->hint = 1;
        latest->lock.Unlock();

        tail->next = latest;
        tail->lock.Unlock();
        return reinterpret_cast<void*>(tail->allocBase);
    }

    bool SlabAlloc::Free(void* ptr)
    {
        for (SlabSegment* seg = head; seg != nullptr; seg = seg->next)
        {
            sl::ScopedLock segLock(seg->lock);
            const size_t segTop = seg->allocBase + (segmentCapacity * slabSize);
            if ((uintptr_t)ptr < seg->allocBase || (uintptr_t)ptr >= segTop)
                continue; //not found in this segment
            
            const uintptr_t localPtr = (uintptr_t)ptr - seg->allocBase;
            if (!sl::BitmapClear(seg->bitmap, localPtr / slabSize))
                Log("Kernel slab double free @ 0x%016lx", LogLevel::Error, (uintptr_t)ptr);
            
            seg->hint = localPtr / slabSize;
            seg->freeCount++;
            //TODO: handle duplicate free segments, free one of them
            return true;
        }

        return true;
    }
}
