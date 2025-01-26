#include <core/Pmm.h>
#include <core/Log.h>
#include <core/Config.h>
#include <core/WiredHeap.h>
#include <interfaces/loader/Generic.h>
#include <Hhdm.h>
#include <UnitConverter.h>
#include <Maths.h>

namespace Npk::Core
{
    /* There isnt a programmatic reason for limiting this to 4 words, but I feel thats
     * a reasonable size for something like `struct PageInfo`, which will exist for every physical page
     * in the system.
     */
    static_assert(sizeof(PageInfo) <= (sizeof(void*) * 4));

    constexpr size_t MemmapChunkSize = 32;
    constexpr size_t LocalListLimit = 64;
    constexpr size_t DefaultVmdWakeFreePages = 512;
    constexpr size_t DefaultVmdWakeFreePercentage = 10;

    void Pmm::IngestMemory(sl::Span<MemmapEntry> entries)
    {
        for (size_t i = 0; i < entries.Size(); i++)
        {
            if (entries[i].length == 0)
                continue;

            const size_t entryPages = entries[i].length >> PfnShift();
            const size_t dbBase = entries[i].base >> PfnShift();
            totalPages += entryPages;

            PageInfo* entry = &infoDb[dbBase];
            entry->pm.count = entryPages;
            entry->pm.zeroed = false;

            sl::ScopedLock scopeLock(globalList.lock);
            globalList.list.PushBack(entry);
            globalList.size += entryPages;

            Log("Physical memory ingested: 0x%tx-0x%tx (0x%zx)", LogLevel::Verbose,
                entries[i].base, entries[i].base + entries[i].length, entries[i].length);
        }
    }

    sl::Opt<uintptr_t> Pmm::AllocFromList(PmList& list)
    {
        sl::ScopedLock scopeLock(list.lock);
        if (list.list.Empty())
            return {};

        PageInfo* allocated = list.list.PopFront();
        list.size--;

        if (wakeVmDaemon != nullptr)
        {
            const bool pageCriteriaMet = list.size < vmdWakeParams.freePages || list.size < vmdWakeParams.freePercent;
            const bool hasSched = GetLocalPtr(SubsysPtr::Scheduler) != nullptr;
            const bool isGlobalList = &list == &globalList;

            if (pageCriteriaMet && hasSched && isGlobalList)
                wakeVmDaemon->Signal(1);
        }

        if (allocated->pm.count > 1)
        {
            PageInfo* heir = allocated + 1;
            heir->pm.count = allocated->pm.count - 1;
            heir->pm.zeroed = allocated->pm.zeroed;
            list.list.PushFront(heir);
        }

        const uintptr_t retAddr = ReverseLookup(allocated);
        if (trashBeforeUse)
            PoisonMemory({ reinterpret_cast<uint8_t*>(retAddr + hhdmBase), PageSize() });
        return retAddr;
    }

    Pmm globalPmm;
    Pmm& Pmm::Global()
    {
        return globalPmm;
    }
    
    void Pmm::Init()
    { 
        globalList.size = 0;
        totalPages = 0;

        trashBeforeUse = GetConfigNumber("kernel.pmm.trash_before_use", false);
        trashAfterUse = GetConfigNumber("kernel.pmm.trash_after_use", false);
        infoDb = reinterpret_cast<PageInfo*>(hhdmBase + hhdmLength);

        MemmapEntry entryStore[MemmapChunkSize];
        size_t usableMemory = 0;
        size_t entriesAccum = 0;
        while (true)
        {
            const size_t count = GetUsableMemmap(entryStore, entriesAccum);
            entriesAccum += count;
            IngestMemory({ entryStore, count });

            for (size_t i = 0; i < count; i++)
                usableMemory += entryStore[i].length;
            if (count < MemmapChunkSize)
                break;
        }

        const auto conv = sl::ConvertUnits(usableMemory);
        Log("Initial usable physical memory: 0x%zx (%zu.%zu %sB), over %zu regions", LogLevel::Info,
            usableMemory, conv.major, conv.minor, conv.prefix, entriesAccum);
    }

    void Pmm::InitLocalCache()
    {
        PmList* localList = NewWired<PmList>();
        VALIDATE_(localList != nullptr, );

        localList->size = 0;
        SetLocalPtr(SubsysPtr::PmmCache, localList);
    }

    void Pmm::ReclaimLoaderMemory()
    { ASSERT_UNREACHABLE(); }

    void Pmm::AttachVmDaemon(Core::Waitable& event)
    {
        ASSERT_(wakeVmDaemon == nullptr);
        wakeVmDaemon = &event;

        const size_t cfgFreePages = GetConfigNumber("kernel.vmd.wake_page_count", DefaultVmdWakeFreePages);
        const size_t cfgFreePercent = GetConfigNumber("kernel.vmd.wake_page_percent", DefaultVmdWakeFreePercentage);

        vmdWakeParams.freePages = cfgFreePages;
        vmdWakeParams.freePercent = totalPages / 100 * sl::Clamp<size_t>(cfgFreePercent, 1, 100);

        Log("Vmd wake params: freePages=0x%zx, percent=%zu%% (%zu pages)", LogLevel::Info, vmdWakeParams.freePages,
            cfgFreePercent, vmdWakeParams.freePercent);
    }

    sl::Opt<uintptr_t> Pmm::Alloc()
    {
        if (CoreLocalAvailable() && GetLocalPtr(SubsysPtr::PmmCache) != nullptr)
        {
            //if per-core lists are available, use that for allocating
            auto& localList = *static_cast<PmList*>(GetLocalPtr(SubsysPtr::PmmCache));

            auto ret = AllocFromList(localList);
            if (ret.HasValue())
                return ret;

            //refill local list from global
            while (localList.size < LocalListLimit)
            {
                auto refill = AllocFromList(globalList);
                if (!refill.HasValue())
                    break;

                sl::ScopedLock scopelock(localList.lock);
                localList.list.PushFront(Lookup(*refill));
                localList.size++;
            }

            return AllocFromList(localList);
        }

        return AllocFromList(globalList);
    }

    void Pmm::Free(uintptr_t paddr)
    {
        VALIDATE_(paddr != 0, );

        if (trashAfterUse)
            PoisonMemory({ reinterpret_cast<uint8_t*>(paddr + hhdmBase), PageSize() });

        PageInfo* pageInfo = Lookup(paddr);
        pageInfo->pm.count = 1;
        pageInfo->pm.zeroed = false;

        if (CoreLocalAvailable() && GetLocalPtr(SubsysPtr::PmmCache) != nullptr)
        {
            auto& localList = *static_cast<PmList*>(GetLocalPtr(SubsysPtr::PmmCache));

            sl::ScopedLock localLock(localList.lock);
            if (localList.size < LocalListLimit) //TODO: we're failing to apply the magazine concept here, fix that
            {
                localList.list.PushFront(pageInfo);
                localList.size++;
                return;
            }
        }

        sl::ScopedLock globalLock(globalList.lock);
        globalList.list.PushFront(pageInfo);
        globalList.size++;
    }
}
