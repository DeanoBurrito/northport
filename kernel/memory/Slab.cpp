#include <memory/Slab.h>
#include <memory/Vmm.h>
#include <debug/Log.h>

namespace Npk::Memory
{
    SlabSegment* SlabAlloc::CreateSegment()
    {
        const size_t totalSize = slabSize * slabsPerSeg;
        auto base = VMM::Kernel().Alloc(totalSize, 0, VmFlag::Anon | VmFlag::Write);
        VALIDATE_(base.HasValue(), nullptr);

        sl::NativePtr scan = *base;
        SlabSegment* seg = new(scan.ptr) SlabSegment();
        seg->base = scan.raw;
        seg->freeCount = slabsPerSeg - (sl::AlignUp(sizeof(SlabSegment), slabSize) / slabSize);
        scan.raw += sl::AlignUp(sizeof(SlabSegment), slabSize);

        for (size_t i = 0; i < seg->freeCount; i++)
        {
            ASSERT_(scan.raw < *base + totalSize);
            seg->entries.PushBack(scan.As<SlabEntry>());
            scan.raw += slabSize;
        }

        Log("Heap slab segment created: size=%lub, count=%lu", LogLevel::Verbose, slabSize, seg->freeCount);
        return seg;
    }

    void SlabAlloc::DestroySegment(SlabSegment* seg)
    {
        const size_t totalSize = slabSize * slabsPerSeg;
        const size_t realSize = sl::AlignUp(sizeof(SlabSegment), slabSize) + seg->freeCount * slabSize;
        ASSERT_(totalSize == realSize);

        VALIDATE_(VMM::Kernel().Free(seg->base), );
    }

    void SlabAlloc::Init(size_t slabBytes, size_t slabCountPerSeg)
    {
        slabSize = slabBytes;
        slabsPerSeg = slabCountPerSeg;
    }

    void* SlabAlloc::Alloc()
    {
        void* foundAddr = nullptr;

        segmentsLock.ReaderLock();
        for (auto seg = segments.Begin(); seg != segments.End(); seg = seg->next)
        {
            seg->lock.Lock();
            if (seg->freeCount == 0)
            {
                seg->lock.Unlock();
                continue;
            }

            foundAddr = seg->entries.PopFront();
            seg->freeCount--;
            ASSERT_(foundAddr != nullptr);
            seg->lock.Unlock();
            break;
        }
        segmentsLock.ReaderUnlock();

        if (foundAddr != nullptr)
            return foundAddr;

        SlabSegment* latest = CreateSegment();
        if (latest == nullptr)
            return nullptr;

        latest->lock.Lock(); //lock only used for memory ordering purposes here
        foundAddr = latest->entries.PopFront();
        latest->freeCount--;
        latest->lock.Unlock();

        segmentsLock.WriterLock();
        segments.PushBack(latest);
        segmentsLock.WriterUnlock();

        return foundAddr;
    }

    bool SlabAlloc::Free(void* ptr)
    {
        const size_t segRange = slabSize * slabsPerSeg;
        const uintptr_t intPtr = reinterpret_cast<uintptr_t>(ptr);

        segmentsLock.ReaderLock();
        for (auto seg = segments.Begin(); seg != segments.End(); seg = seg->next)
        {
            if (intPtr < seg->base || intPtr >= seg->base + segRange)
                continue;

            seg->lock.Lock();
            seg->entries.PushBack(static_cast<SlabEntry*>(ptr));
            seg->freeCount++;
            seg->lock.Unlock();

            segmentsLock.ReaderUnlock();
            return true;
        }
        segmentsLock.ReaderUnlock();

        return false;
    }
}
