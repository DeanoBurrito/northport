#include <core/Pmm.h>
#include <core/Log.h>
#include <core/Config.h>
#include <interfaces/loader/Generic.h>
#include <Hhdm.h>
#include <UnitConverter.h>

namespace Npk::Core
{
    constexpr size_t MemmapChunkSize = 32;

    void Pmm::IngestMemory(sl::Span<MemmapEntry> entries)
    {
        for (size_t i = 0; i < entries.Size(); i++)
        {
            const size_t entryPages = entries[i].length / PageSize();
            const size_t dbBase = entries[i].base / PageSize();

            for (size_t i = 0; i < entryPages; i++)
                infoDb[dbBase + i].flags = PmFlags::None;

            PmFreeEntry* listHead = reinterpret_cast<PmFreeEntry*>(entries[i].base + hhdmBase);
            listHead->next = nullptr;
            listHead->count = entryPages;

            freelist.lock.Lock();
            listHead->next = freelist.head;
            freelist.head = listHead;
            freelist.size += entryPages;
            freelist.lock.Unlock();

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
        freelist.head = nullptr;
        freelist.size = 0;
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
        paddr /= PageSize();
        return infoDb + paddr;
    }

    sl::Opt<uintptr_t> Pmm::Alloc()
    {
        //TODO: per-core caches
        sl::ScopedLock listLock(freelist.lock);
        if (freelist.head == nullptr)
            return {};

        freelist.size--;
        uintptr_t allocAddr = reinterpret_cast<uintptr_t>(SubHhdm(freelist.head));
        if (freelist.head->count == 1)
            freelist.head = freelist.head->next;
        else
        {
            const size_t nextCount = freelist.head->count - 1;
            freelist.head++;
            freelist.head->count = nextCount;
        }

        Lookup(allocAddr)->flags = PmFlags::None;
        return allocAddr;
    }

    void Pmm::Free(uintptr_t paddr)
    {
        VALIDATE_(paddr != 0, );

        PmFreeEntry* listEntry = reinterpret_cast<PmFreeEntry*>(paddr + hhdmBase);
        listEntry->count = 1;

        sl::ScopedLock listLock(freelist.lock);
        freelist.size++;
        listEntry->next = freelist.head;
        freelist.head = listEntry;
    }
}
