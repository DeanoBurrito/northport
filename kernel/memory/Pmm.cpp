#include <memory/Pmm.h>
#include <boot/LimineTags.h>
#include <config/ConfigStore.h>
#include <debug/Log.h>
#include <Bitmap.h>
#include <Maths.h>
#include <Memory.h>
#include <CppUtils.h>
#include <UnitConverter.h>

namespace Npk::Memory
{
    constexpr size_t PmMaxContiguousPages = (64 * MiB) / PageSize;
    constexpr size_t PmMinContiguousPages = 0;
    constexpr size_t PmContiguousPagesRatio = 10;

    PMM globalPmm;
    PMM& PMM::Global()
    { return globalPmm; }

    static void SortMemoryMapBySize(size_t count, limine_memmap_entry** entries)
    {
        //bubble sort, caught in the wild
        for (size_t i = 0; i < count - 1; i++)
        {
            for (size_t j = 0; j < count - i - 1; j++)
            {
                if (entries[j]->length < entries[j + 1]->length)
                    sl::Swap(entries[j], entries[j + 1]);
            }
        }
    }

    static uintptr_t ClaimFromSmallest(size_t count, limine_memmap_entry** entries, size_t pages)
    {
        const size_t bytes = pages * PageSize;

        for (size_t i = count; i > 0; i--)
        {
            const size_t idx = i - 1;
            if (entries[idx]->length < bytes)
                continue;

            const uintptr_t base = entries[idx]->base;
            entries[idx]->base += bytes;
            entries[idx]->length -= bytes;
            return base;
        }

        return 0;
    }

    void PMM::IngestMemory(size_t entryCount, limine_memmap_entry** entries, size_t contiguousQuota)
    {
        size_t totalPages = 0;
        for (size_t i = 0; i < entryCount; i++)
            totalPages += entries[i]->length / PageSize;

        SortMemoryMapBySize(entryCount, entries);

        //reserve contiguous regions, starting from largest chunk of physical memory
        zones = nullptr;
        size_t contigZones = 0;
        for (size_t i = 0; i < contiguousQuota;)
        {
            const size_t count = sl::Min(contiguousQuota - i, (size_t)entries[0]->length / PageSize);
            i += count;
            entries[0]->length -= count * PageSize;

            const size_t bitmapBytes = sl::AlignUp(count, 8) / 8;
            const size_t metadataBytes = bitmapBytes + (2 * sizeof(PmContigZone));
            const uintptr_t metadataAddr = ClaimFromSmallest(entryCount, entries, sl::AlignUp(metadataBytes, PageSize) / PageSize);
            ASSERT_(metadataAddr != 0);

            PmContigZone* zone = reinterpret_cast<PmContigZone*>(sl::AlignUp(metadataAddr + hhdmBase, sizeof(PmContigZone)));
            new(zone) PmContigZone();
            zone->base = entries[0]->base + entries[0]->length;
            zone->count = count;
            zone->bitmap = reinterpret_cast<uint8_t*>(zone + 1);
            sl::memset(zone->bitmap, 0, bitmapBytes);

            zonesLock.WriterLock();
            zone->next = zones;
            zones = zone;
            zonesLock.WriterUnlock();

            contigZones++;
            Log("Contiguous physical memory zone: base=0x%lx, count=%lu", LogLevel::Verbose, zone->base,  zone->count);
            SortMemoryMapBySize(entryCount, entries); //sort memory map again, since we just modified it.
        }

        //allocate space for pageinfo segments
        const size_t segmentStoreBytes = sizeof(PmInfoSegment) * (entryCount + contigZones + 1);
        const size_t infoStoreSize = segmentStoreBytes + sizeof(PageInfo) * (totalPages+ 1);
        uintptr_t infoStoreAddr = ClaimFromSmallest(entryCount, entries, 
            sl::AlignUp(infoStoreSize, PageSize) / PageSize);
        ASSERT_(infoStoreAddr != 0);
        infoStoreAddr = sl::AlignUp(infoStoreAddr, sizeof(PmInfoSegment)) + hhdmBase;

        size_t segmentIndex = 0;
        size_t infoIndex = 0;
        auto segmentStore = reinterpret_cast<PmInfoSegment*>(infoStoreAddr);
        auto infoStore = reinterpret_cast<PageInfo*>(sl::AlignUp(infoStoreAddr + segmentStoreBytes, sizeof(PageInfo)));
        segments = nullptr;

        //create mappings from usable regions to PageInfo database
        PmInfoSegment* tail = nullptr;
        for (size_t i = 0; i < entryCount; i++)
        {
            if (entries[i]->length == 0)
                continue;

            PmInfoSegment* segment = &segmentStore[segmentIndex++];
            new(segment) PmInfoSegment();
            segment->base = entries[i]->base;
            segment->length = entries[i]->length;
            segment->info = infoStore + infoIndex;
            infoIndex += segment->length / PageSize;

            if (tail == nullptr)
                tail = segment;
            else
            {
                tail->next = segment;
                tail = segment;
            }

            Log("PageInfo segment added: base=0x%lx, count=%lu, store=%p", LogLevel::Verbose,
                segment->base, segment->length / PageSize, segment->info);
        }

        //also create info mappings for pages in contiguous zones
        for (PmContigZone* zone = zones; zone != nullptr; zone = zone->next)
        {
            PmInfoSegment* segment = &segmentStore[segmentIndex++];
            new(segment) PmInfoSegment();
            segment->base = zone->base;
            segment->length = zone->count * PageSize;
            segment->info = infoStore + infoIndex;
            infoIndex += segment->length / PageSize;

            if (tail == nullptr)
                tail = segment;
            else
            {
                tail->next = segment;
                tail = segment;
            }

            Log("PageInfo (contig zone) segment added: base=0x%lx, count=%lu, store=%p", LogLevel::Verbose,
                segment->base, segment->length / PageSize, segment->info);
        }

        segmentsLock.WriterLock();
        if (segments == nullptr)
            segments = tail;
        else
        {
            PmInfoSegment* currentTail = segments;
            while (currentTail->next != nullptr)
                currentTail = segments->next;
            currentTail->next = tail;
        }
        segmentsLock.WriterUnlock();

        //all other physical memory is added to the freelist
        freelist.head = nullptr;
        for (size_t i = 0; i < entryCount; i++)
        {
            if (entries[i]->length == 0)
                continue;

            auto entry = reinterpret_cast<PmFreeEntry*>(entries[i]->base + hhdmBase);
            entry->runLength = entries[i]->length / PageSize;

            entry->next = freelist.head;
            freelist.head = entry;
        }
    }

    void PMM::Init()
    {
        auto mmapResp = Boot::memmapRequest.response;

        //scan the memory map and figure out how many usable entries and pages there are.
        size_t usablePages = 0;
        size_t usableEntries = 0;
        limine_memmap_entry* usableMap[mmapResp->entry_count];
        for (size_t i = 0; i < mmapResp->entry_count; i++)
        {
            limine_memmap_entry* entry = mmapResp->entries[i];
            if (entry->type != LIMINE_MEMMAP_USABLE)
                continue;

            usablePages += entry->length / PageSize;
            usableMap[usableEntries++] = entry;
        }

        auto conv = sl::ConvertUnits(usablePages * PageSize, sl::UnitBase::Binary);
        Log("PMM has %lu usable pages (%lu.%lu %sB), over %lu regions.", LogLevel::Info, usablePages,
            conv.major, conv.minor, conv.prefix, usableEntries);

        //determine how much physical ram to reserve for contiguous allocations
        size_t contiguousPages = usablePages / PmContiguousPagesRatio;
        contiguousPages = sl::Min(PmMaxContiguousPages, sl::Max(PmMinContiguousPages, contiguousPages)); //ensure its within bounds
        contiguousPages = sl::Min(contiguousPages, usablePages); //ensure its actually valid after all that
        Log("%lu pages reserved for contiguous allocs (pratio=1/%lu, min=%lu, max=%lu)", LogLevel::Info,
            contiguousPages, PmContiguousPagesRatio, PmMinContiguousPages, PmMaxContiguousPages);

        trashAfterUse = Config::GetConfigNumber("kernel.pmm.trash_after_use", false);
        trashBeforeUse = Config::GetConfigNumber("kernel.pmm.trash_before_use", false);
        if (trashAfterUse || trashBeforeUse)
            new (&rng) sl::XoshiroRng();

        IngestMemory(usableEntries, usableMap, contiguousPages);
    }

    void PMM::ReclaimBootMemory()
    {
        auto mmapResp = Boot::memmapRequest.response;
        size_t totalLength = 0; //not needed here, I'm just being vain.

        //when reclaiming the bootloader memory we need our own copy of the memory map
        //(or at least the entries we care about) since the memory map is inside the regions
        //we're reclaiming - which may be overwritten with control data for the PMM.
        //So we make a copy of the reclaimable memory map entries on the stack and use that.
        size_t entryCount = 0;
        limine_memmap_entry entriesStore[mmapResp->entry_count];
        limine_memmap_entry* entries[mmapResp->entry_count];
        for (size_t i = 0; i < mmapResp->entry_count; i++)
        {
            if (mmapResp->entries[i]->type != LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE)
                continue;

            entriesStore[entryCount] = *mmapResp->entries[i];
            entries[entryCount] = entriesStore + entryCount;
            totalLength += entries[entryCount]->length;
            entryCount++;
        }

        IngestMemory(entryCount, entries, 0);
        auto conv = sl::ConvertUnits(totalLength, sl::UnitBase::Binary);
        Log("Bootloader memory reclaimed: %lu.%lu %sB (%lu entries)", LogLevel::Info,
            conv.major, conv.minor, conv.prefix, entryCount);
    }

    PageInfo* PMM::Lookup(uintptr_t physAddr)
    {
        for (PmInfoSegment* scan = segments; scan != nullptr; scan = scan->next)
        {
            if (physAddr < scan->base || physAddr >= scan->base + scan->length)
                continue;

            const size_t index = (physAddr - scan->base) / PageSize;
            return scan->info + index;
        }

        return nullptr;
    }

    uintptr_t PMM::Alloc(size_t count)
    {
        if (count == 0)
            return 0;
        if (count == 1)
        {
            sl::ScopedLock scopeLock(freelist.lock);
            auto freeEntry = freelist.head;
            ASSERT_(freeEntry != nullptr);

            if (freeEntry->runLength > 1)
            {
                const uintptr_t nextHead = reinterpret_cast<uintptr_t>(freeEntry) + PageSize;
                freelist.head = reinterpret_cast<PmFreeEntry*>(nextHead);
                freelist.head->runLength = freeEntry->runLength - 1;
                freelist.head->next = freeEntry->next;
            }
            else
                freelist.head = freeEntry->next;

            if (trashBeforeUse)
            {
                uint64_t* access = reinterpret_cast<uint64_t*>(freeEntry);
                for (size_t i = 0; i < PageSize / sizeof(uint64_t); i++)
                    access[i] = rng.Next();
            }
            return reinterpret_cast<uintptr_t>(freeEntry) - hhdmBase;
        }

        for (PmContigZone* zone = zones; zone != nullptr; zone = zone->next)
        {
            size_t runningCount = 0;
            size_t runStartIndex = 0;

            sl::ScopedLock zoneLock(zone->lock);
            for (size_t i = 0; i < zone->count; i++)
            {
                if (sl::BitmapGet(zone->bitmap, i))
                {
                    runningCount = 0;
                    continue;
                }

                if (runningCount == 0)
                    runStartIndex = i;
                runningCount++;
                if (runningCount == count)
                    break;
            }

            if (runningCount != count)
                continue;

            for (size_t i = runStartIndex; i < runStartIndex + count; i++)
                sl::BitmapSet(zone->bitmap, i);

            if (trashBeforeUse)
            {
                uint64_t* access = reinterpret_cast<uint64_t*>(zone->base + (runStartIndex * PageSize));
                for (size_t i = 0; i < PageSize / sizeof(uint64_t); i++)
                    access[i] = rng.Next();
            }
            return zone->base + (runStartIndex * PageSize);
        }

        return 0;
    }

    void PMM::Free(uintptr_t base, size_t count)
    {
        if (base == 0 || count == 0)
            return;
        if ((base & (PageSize - 1)) != 0)
        {
            Log("Free of misaligned physical address: 0x%lx", LogLevel::Error, base);
            base = base & ~(PageSize - 1);
        }

        /* If the freelist cant satisfy a single-page allocation, the pmm will use a contiguous zone,
         * so we cant blindly put pages back into the freelist as they may have come from a contig zone.
         * Fortunately its quite cheap to check this as we know the bounds of each contig zone,
         * and on most systems there should only be 1 zone anyway.
         */
        for (PmContigZone* zone = zones; zone != nullptr; zone = zone->next)
        {
            if (base < zone->base || base >= zone->base + (zone->count * PageSize))
                continue;

            const size_t beginIndex = (base - zone->base) / PageSize;
            sl::ScopedLock scopeLock(zone->lock);
            for (size_t i = 0; i < count; i++)
            {
                if (!sl::BitmapClear(zone->bitmap, beginIndex + i))
                    Log("Double free of physical page 0x%lx", LogLevel::Error, base + (i * PageSize));

                if (trashAfterUse)
                {
                    uint64_t* access = reinterpret_cast<uint64_t*>(base + hhdmBase);
                    for (size_t i = 0; i < PageSize / sizeof(uint64_t); i++)
                        access[i] = rng.Next();
                }
            }
            return;
        }

        if (count != 1)
        {
            Log("Free of %lu physical pages at 0x%lx from unknown contiguous zone", LogLevel::Error,
                count, base);
            return;
        }

        //TOOD: PMM accounting, allow setting of a flag that checks ALL phys addresses freed, by scanning the 
        //info segment mappings - rather than adding straight to the freelist. Would be slow, but useful for debugging.
        auto freeEntry = reinterpret_cast<PmFreeEntry*>(base + hhdmBase);
        freeEntry->runLength = 1;

        sl::ScopedLock scopeLock(freelist.lock);
        freeEntry->next = freelist.head;
        freelist.head = freeEntry;

        if (trashAfterUse)
        {
            uint64_t* access = reinterpret_cast<uint64_t*>(base + hhdmBase);
            for (size_t i = 0; i < PageSize / sizeof(uint64_t); i++)
                access[i] = rng.Next();
        }
    }
}
