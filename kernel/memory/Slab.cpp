#include <memory/Slab.h>
#include <memory/Vmm.h>
#include <debug/Log.h>
#include <Random.h>

namespace Npk::Memory
{
    constexpr uint8_t PoisonValue = 0xA5;

    extern sl::XoshiroRng trashGenerator; //defined in Pool.cpp
    extern bool doBoundsCheck;
    extern bool trashAfterUse;
    extern bool trashBeforeUse;
    extern bool logHeapExpansion;

    SlabSegment* SlabAlloc::CreateSegment()
    {
        const size_t totalSize = slabSize * slabsPerSeg;
        auto base = VMM::Kernel().Alloc(totalSize, 0, VmFlag::Anon | VmFlag::Write);
        VALIDATE_(base.HasValue(), nullptr);

        sl::NativePtr scan = *base;
        SlabSegment* seg = new(scan.ptr) SlabSegment();
        seg->base = scan.raw;
        seg->freeCount = slabsPerSeg - (sl::AlignUp(sizeof(SlabSegment), slabSize) / slabSize);
        if (doBoundsCheck)
            seg->freeCount /= 2;
        scan.raw += sl::AlignUp(sizeof(SlabSegment), slabSize);

        for (size_t i = 0; i < seg->freeCount; i++)
        {
            ASSERT_(scan.raw < *base + totalSize);
            seg->entries.PushBack(scan.As<SlabEntry>());
            scan.raw += slabSize;

            if (doBoundsCheck)
            {
                sl::memset(scan.As<uint8_t>(), PoisonValue, slabSize);
                scan.raw += slabSize;
            }
        }

        if (logHeapExpansion)
        {
            Log("Heap slab segment created: size=%ub, count=%zu%s", LogLevel::Verbose, slabSize, seg->freeCount,
                doBoundsCheck ? ", bounds-checking enabled" : "");
        }
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
        CheckBounds();
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

        if (foundAddr == nullptr)
        {
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
        }

        if (trashBeforeUse)
        {
            uint64_t* access = static_cast<uint64_t*>(foundAddr);
            for (size_t i = 0; i < slabSize / sizeof(uint64_t); i++)
                *access = trashGenerator.Next();
        }

        return foundAddr;
    }

    bool SlabAlloc::Free(void* ptr)
    {
        CheckBounds();

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

            if (doBoundsCheck)
                sl::memset(sl::NativePtr(ptr).Offset(slabSize).ptr, PoisonValue, slabSize);
            if (trashAfterUse)
            {
                uint64_t* access = static_cast<uint64_t*>(ptr);
                for (size_t i = 0; i < slabSize / sizeof(uint64_t); i++)
                    *access = trashGenerator.Next();
            }
            seg->lock.Unlock();

            segmentsLock.ReaderUnlock();
            return true;
        }
        segmentsLock.ReaderUnlock();

        return false;
    }

    void SlabAlloc::CheckBounds()
    {
        if (!doBoundsCheck)
            return;

        segmentsLock.ReaderLock();
        for (auto seg = segments.Begin(); seg != segments.End(); seg = seg->next)
        {
            sl::NativePtr scan = seg->base;
            const uintptr_t segmentTop = scan.raw + slabsPerSeg * slabSize;
            scan.raw += sl::AlignUp(sizeof(SlabSegment), slabSize);

            while (scan.raw + (slabSize * 2) < segmentTop)
            {
                uint8_t* poison = scan.Offset(slabSize).As<uint8_t>();

                for (size_t i = 0; i < slabSize; i++)
                {
                    if (poison[i] == PoisonValue)
                        continue;

                    Log("Slab overrun at 0x%tx (size=%u)", LogLevel::Warning, scan.raw, slabSize);
                    break;
                }

                scan.raw += slabSize * 2;
            }
        }
        segmentsLock.ReaderUnlock();
    }
}
