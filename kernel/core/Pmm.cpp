#include <core/Pmm.h>
#include <core/Log.h>
#include <core/Config.h>
#include <interfaces/loader/Generic.h>
#include <Hhdm.h>
#include <UnitConverter.h>

namespace Npk::Core
{
    static_assert(sizeof(PageInfo) <= (sizeof(void*) * 4));

    constexpr size_t MemmapChunkSize = 32;

    void Pmm::IngestMemory(sl::Span<MemmapEntry> entries)
    {
        for (size_t i = 0; i < entries.Size(); i++)
        {
            const size_t entryPages = entries[i].length / PageSize();
            const size_t dbBase = entries[i].base / PageSize();

            for (size_t i = 0; i < entryPages; i++)
                new(infoDb + dbBase + i) PageInfo();

            PageInfo* entry = &infoDb[dbBase];
            entry->pm.count = entryPages;

            listLock.Lock();
            list.PushBack(entry);
            listSize += entryPages;
            listLock.Unlock();

            Log("Physical memory ingested: 0x%tx-0x%tx (0x%zx)", LogLevel::Verbose,
                entries[i].base, entries[i].base + entries[i].length, entries[i].length);
        }
    }

    Pmm globalPmm;
    Pmm& Pmm::Global()
    {
        return globalPmm;
    }
    
    void Pmm::Init()
    { 
        listSize = 0;

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
        Log("Initial usable physical memory: %zu.%zu %sB, over %zu regions", LogLevel::Info,
            conv.major, conv.minor, conv.prefix, entriesAccum);
    }

    void Pmm::InitLocalCache()
    { ASSERT_UNREACHABLE(); }

    void Pmm::ReclaimLoaderMemory()
    { ASSERT_UNREACHABLE(); }

    PageInfo* Pmm::Lookup(uintptr_t paddr)
    {
        return infoDb + (paddr >> PfnShift());
    }

    sl::Opt<uintptr_t> Pmm::Alloc()
    {
        //TODO: per-core caches
        sl::ScopedLock scopeLock(listLock);
        if (list.Empty())
            return {};

        PageInfo* allocated = list.PopFront();
        listSize--;
        const uintptr_t retAddr = (allocated - infoDb) << PfnShift();

        if (allocated->pm.count != 1)
        {
            PageInfo* heir = allocated + 1;
            heir->pm.count = allocated->pm.count - 1;
            list.PushFront(heir);
        }
        allocated->pm.count = 0;

        if (trashBeforeUse)
            PoisonMemory({ reinterpret_cast<uint8_t*>(retAddr + hhdmBase), PageSize() });
        return retAddr;
    }

    void Pmm::Free(uintptr_t paddr)
    {
        VALIDATE_(paddr != 0, );

        if (trashAfterUse)
            PoisonMemory({ reinterpret_cast<uint8_t*>(paddr + hhdmBase), PageSize() });

        PageInfo* pageInfo = Lookup(paddr);
        pageInfo->pm.count = 1;

        sl::ScopedLock scopeLock(listLock);
        listSize++;
        list.PushFront(pageInfo); //TODO: trash after use
    }
}