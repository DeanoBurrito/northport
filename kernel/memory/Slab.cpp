#include <memory/Slab.h>
#include <arch/Platform.h>
#include <arch/Paging.h>
#include <debug/Log.h>
#include <memory/Pmm.h>
#include <memory/Vmm.h>
#include <Bitmap.h>
#include <Memory.h>

namespace Npk::Memory
{
    SlabSegment* SlabAlloc::InitSegment(uintptr_t base)
    {
        const size_t totalSize = slabSize * slabsPerSeg;
        const size_t bitmapSize = sl::AlignUp(slabsPerSeg, 8) / 8;
        const size_t metaSize = sl::AlignUp(sizeof(SlabSegment) + bitmapSize, slabSize);
        const size_t slabCount = slabsPerSeg - metaSize / slabSize;

        //behaviour changes if we're a pinned slab: modify paging structure directly, otherwise use the VMM.
        //Since the VMM needs to allocate VMRanges for VMM::Alloc() to function, the initial pinned slabs 
        //can't rely on the VMM as this creates a circular dependency.
        //Pinned slabs are also used for other critical/non-swappable kernel data (process/thread/core control blocks)
        if (pinned)
        {
            //TODO: check we dont overflow our reserved space above the hhdm, and handle that.
            for (size_t i = 0; i < totalSize; i += PageSize)
                MapMemory(kernelMasterTables, base + i, PMM::Global().Alloc(), PageFlags::Write, PageSizes::_4K, false);
            kernelTablesGen.Add(1, sl::Release);
        }
        else
        {
            auto maybeRegion = VMM::Kernel().Alloc(totalSize, 0, VmFlags::Write | VmFlags::Anon);
            ASSERT(maybeRegion, "Failed to expand kernel slabs.");
            base = maybeRegion->base;
        }

        uint8_t* bitmapBase = reinterpret_cast<uint8_t*>(base + sizeof(SlabSegment));
        SlabSegment* segment = new((void*)base) SlabSegment(base + metaSize, bitmapBase, slabCount);
        sl::memset(bitmapBase, 0, bitmapSize);

        Log("New kslab seg: base=0x%016lx, slabSize=%luB, count=%lu (%lu reserved), pinned=%s", LogLevel::Verbose,
            base + metaSize, slabSize, slabCount, slabsPerSeg - slabCount, pinned ? "yes" : "no");
        
        return segment;
    }

    void SlabAlloc::Init(uintptr_t firstSegment, size_t sizeSize, size_t slabCount)
    {
        slabSize = sizeSize;
        slabsPerSeg = slabCount;
        pinned = (firstSegment != 0);

        head = tail = InitSegment(firstSegment);
    }

    void* SlabAlloc::Alloc(uintptr_t& expansionBase)
    {
        sl::ScopedLock scopeLock(lock);

        for (SlabSegment* seg = head; seg != nullptr; seg = seg->next)
        {
            if (seg->freeCount == 0)
                continue;
            
            while (sl::BitmapGet(seg->bitmap, seg->hint))
            {
                seg->hint++;
                if (seg->hint >= seg->totalCount)
                    seg->hint = 0;
            }

            sl::BitmapSet(seg->bitmap, seg->hint);
            seg->freeCount--;
            return reinterpret_cast<void*>(seg->allocBase + seg->hint * slabSize);
        }

        tail->next = InitSegment(expansionBase);
        tail = tail->next;

        if (pinned)
            expansionBase += sl::AlignUp(slabSize * slabsPerSeg, PageSize);

        //dont even bother searching, just take the first slab
        sl::BitmapSet(tail->bitmap, 0);
        tail->freeCount--;
        tail->hint = 1;
        return reinterpret_cast<void*>(tail->allocBase);
    }

    bool SlabAlloc::Free(void* ptr)
    {
        sl::ScopedLock scopeLock(lock);

        for (SlabSegment* seg = head; seg != nullptr; seg = seg->next)
        {
            const uintptr_t segTop = seg->allocBase + seg->totalCount * slabSize;
            if ((uintptr_t)ptr >= segTop)
                continue;
            
            if ((uintptr_t)ptr < seg->allocBase)
                return false;

            const uintptr_t localPtr = (uintptr_t)ptr - seg->allocBase;
            if (!sl::BitmapClear(seg->bitmap, localPtr / slabSize))
                Log("Kernel slab %sdouble free @ 0x%016lx", LogLevel::Error, pinned ? "(pinned) " : "", (uintptr_t)ptr);
            
            seg->hint = localPtr / slabSize;
            seg->freeCount++;
            return true;
        }

        return false;
    }
}
