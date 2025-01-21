#include <core/WiredHeap.h>
#include <core/Log.h>
#include <core/Pmm.h>
#include <core/Config.h>
#include <Hhdm.h>
#include <containers/List.h>
#include <Maths.h>

namespace Npk::Core
{
    struct FreeSlab
    {
        sl::FwdListHook next;
    };

    constexpr size_t BaseSlabSize = sl::Max<size_t>(sizeof(FreeSlab), 32);
    constexpr size_t SlabCount = 4;

    sl::RunLevelLock<RunLevel::Dpc> slabLocks[SlabCount];
    sl::FwdList<PageInfo, &PageInfo::mmList> slabs[SlabCount];
    bool wiredTrashBefore;
    bool wiredTrashAfter;

    using SlabFreelist = sl::FwdList<FreeSlab, &FreeSlab::next>; //the object inside PageInfo.slab.list
    static_assert(sizeof(SlabFreelist) <= sizeof(PageInfo::slab));

    static void InitSlab(uintptr_t paddr, size_t elementSize)
    {
        PageInfo* meta = PmLookup(paddr);
        meta->slab.used = 0;

        SlabFreelist* freelist = new(meta->slab.list) SlabFreelist();
        const size_t elementCount = PageSize() / elementSize;
        for (size_t i = 0; i < elementCount; i++)
            freelist->PushBack(reinterpret_cast<FreeSlab*>(hhdmBase + paddr + i * elementSize));

        Log("Wired slab added: base=0x%tx, size=%zu B, count=%zu", LogLevel::Verbose,
            paddr, elementSize, elementCount);
    }

    void InitWiredHeap()
    {
        wiredTrashBefore = Core::GetConfigNumber("kernel.heap.trash_before_use", false);
        wiredTrashAfter = Core::GetConfigNumber("kernel.heap.trash_after_use", false);
    }

    void InitLocalHeapCache()
    { ASSERT_UNREACHABLE(); }

    void* WiredAlloc(size_t size)
    {   
        VALIDATE_(size != 0, nullptr);

        size = sl::AlignUpBinary(size) / BaseSlabSize;
        const size_t slabIndex = size == 0 ? 0 : __builtin_ctzl(size);
        VALIDATE_(slabIndex < SlabCount, nullptr);

        if (CoreLocalAvailable())
            VALIDATE_(CurrentRunLevel() == RunLevel::Normal, nullptr);

        //TODO: per-core caches
        sl::ScopedLock listLock(slabLocks[slabIndex]);
        for (auto it = slabs[slabIndex].Begin(); it != slabs[slabIndex].End(); ++it)
        {
            SlabFreelist* freelist = reinterpret_cast<SlabFreelist*>(it->slab.list);
            if (freelist->Empty()) //TODO: separate list for full slablists so we dont search them?
                continue;

            it->slab.used++;
            return freelist->PopFront();
        }

        auto allocatedPage = PmAlloc();
        VALIDATE_(allocatedPage.HasValue(), nullptr);

        InitSlab(*allocatedPage, BaseSlabSize << slabIndex);
        PageInfo* pageInfo = PmLookup(*allocatedPage);
        SlabFreelist* freelist = reinterpret_cast<SlabFreelist*>(pageInfo->slab.list);

        pageInfo->slab.used++;
        void* allocatedAddr = freelist->PopFront();
        slabs[slabIndex].PushFront(pageInfo);

        if (wiredTrashBefore)
            PoisonMemory({ (uint8_t*)allocatedAddr, BaseSlabSize << slabIndex });

        return allocatedAddr;
    } 

    void WiredFree(void* ptr, size_t size)
    {
        if (ptr == nullptr || size == 0 || size >= (BaseSlabSize << SlabCount))
            return;

        size = sl::AlignUpBinary(size) / BaseSlabSize;
        const size_t slabIndex = size == 0 ? 0 : __builtin_ctzl(size);
        VALIDATE_(slabIndex < SlabCount, );

        if (CoreLocalAvailable())
            VALIDATE_(CurrentRunLevel() == RunLevel::Normal, );
        if (wiredTrashAfter)
            PoisonMemory({ (uint8_t*)ptr, BaseSlabSize << slabIndex });

        const void* slabPage = SubHhdm(AlignDownPage(ptr));
        PageInfo* slabInfo = PmLookup(reinterpret_cast<uintptr_t>(slabPage));
        SlabFreelist* freelist = reinterpret_cast<SlabFreelist*>(slabInfo->slab.list);

        sl::ScopedLock listLock(slabLocks[slabIndex]);

        freelist->PushFront(static_cast<FreeSlab*>(ptr));
        slabInfo->slab.used--;
        if (slabInfo->slab.used == 0)
        {
            Log("Wired slab removed: base=%p , size=%zu B", LogLevel::Verbose,
                slabPage, BaseSlabSize << slabIndex);
            PmFree(reinterpret_cast<uintptr_t>(slabPage));
        }
    }
}

void* operator new(size_t size)
{ ASSERT_UNREACHABLE(); (void)size; } //TODO: move these to executive heap, for paged objects

void* operator new[](size_t size)
{ ASSERT_UNREACHABLE(); (void)size; }
