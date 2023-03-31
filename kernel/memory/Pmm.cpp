#include <memory/Pmm.h>
#include <boot/LimineTags.h>
#include <Memory.h>
#include <Maths.h>
#include <debug/Log.h>
#include <UnitConverter.h>

namespace Npk::Memory
{
    constexpr const char* MemmapTypeStrs[] = 
    {
        "usable", "reserved", "acpi reclaim", "acpi nvs",
        "bad", "bootloader reclaim", "kernel/modules", "framebuffer"
    };

    void PMM::InsertRegion(PmZone& zone, uintptr_t base, size_t length)
    {
        ASSERT(base % PageSize == 0, "PMRegion base not page-aligned.");
        ASSERT(length % PageSize == 0, "PMRegion length not page-aligned.");

        //allocate space for this region to store PageInfos
        if (remainingPageInfoAllocs < length / PageSize)
        {
            const size_t requiredPages = sl::AlignUp(length / PageSize, PageSize) / PageSize;
            ASSERT(length > requiredPages, "Not enough space");
            Log("Expanding PageInfo slab by %lu pages.", LogLevel::Info, requiredPages);
            pageInfoAlloc = reinterpret_cast<PageInfo*>(base + hhdmBase);
            remainingPageInfoAllocs = (requiredPages * PageSize) / sizeof(PageInfo);
            base += requiredPages * PageSize;
            length -= requiredPages * PageSize;
        }
        PageInfo* infoStore = pageInfoAlloc; //TODO: expand pageInfo store if needed
        pageInfoAlloc += length / PageSize;
        sl::memset(infoStore, 0, length / PageSize);

        //allocate the region, initialize it
        if (remainingRegionAllocs == 0)
        {
            ASSERT(length > PageSize, "Not enough space");
            Log("Expanding PmRegion slab by 1 page.", LogLevel::Info);
            regionAlloc = reinterpret_cast<PmRegion*>(base + hhdmBase);
            remainingRegionAllocs = PageSize / sizeof(PmRegion);
            base += PageSize;
            length -= PageSize;
        }

        PmRegion* latest = new(regionAlloc++) PmRegion(base, length, infoStore);
        remainingRegionAllocs -= 1;

        sl::ScopedLock latestLock(latest->lock);

        //find it's place in the list
        PmRegion* next = zone.head;
        PmRegion* prev = nullptr;
        while (next != nullptr && next->base < base)
        {
            prev = next;
            next = next->next;
        }

        if (next == nullptr)
        {
            if (zone.tail != nullptr)
            {
                sl::ScopedLock scopeLock(zone.tail->lock);
                zone.tail->next = latest;
            }
            zone.tail = latest;
            if (zone.head == nullptr)
                zone.head = latest;
        }
        else if (prev == nullptr)
        {
            latest->next = zone.head;
            zone.head = latest;
        }
        else
        {
            latest->next = next;
            prev->next = latest;
        }

        zone.total.Add(latest->totalPages, sl::Relaxed);

        auto conversion = sl::ConvertUnits(latest->totalPages * PageSize, sl::UnitBase::Binary);
        Log("PMRegion inserted: base=0x%08lx, pages=%lu (%lu.%lu%sB).", LogLevel::Info, latest->base, latest->totalPages,
            conversion.major, conversion.minor, conversion.prefix);
    }

    uintptr_t PMM::RegionAlloc(PmRegion& region, size_t count)
    {
        if (region.freePages < count)
            return 0;
        
        sl::ScopedLock scopeLock(region.lock);
        size_t start = region.searchHint;
        size_t end = region.totalPages;

    try_region_alloc_search:
        for (size_t i = start; i < end; i++)
        {
            if (i + count >= end)
                break;
            
            bool usable = true;
            for (size_t mod = 0; mod < count; mod++)
            {
                if (region.infoBuffer[i + mod].flags.HasBits(PmFlags::Used))
                {
                    i += mod;
                    usable = false;
                    break;
                }
            }

            if (!usable)
                continue;
            
            for (size_t j = 0; j < count; j++)
                region.infoBuffer[i + j].flags.SetBits(PmFlags::Used);
            
            region.freePages -= count;
            region.searchHint = (i + count) % region.totalPages;
            return region.base + (i * PageSize);
        }

        if (start == region.searchHint && region.searchHint != 0)
        {
            start = 0;
            end = region.searchHint;
            goto try_region_alloc_search;
        }
        return 0;
    }
    
    PMM globalPmm;
    PMM& PMM::Global()
    { return globalPmm; }
    
    void PMM::Init()
    {
        //these variables *should* be default initialized via the .bss, but
        //*just in case* lets initialize them ourselves.
        remainingRegionAllocs = 0;
        remainingPageInfoAllocs = 0;
        new(&zones[0]) PmZone(); //low zone (addresses < 4GiB)
        new(&zones[1]) PmZone(); //high zone (addresses >= 4GiB)

        const size_t mmapEntryCount = Boot::memmapRequest.response->entry_count;
        limine_memmap_entry** const mmapEntries = Boot::memmapRequest.response->entries;

        //We're going to allocate a PmRegion struct for each usable (and later reclaimable)
        //region of the memory map, so determine how many we need.
        //While iterating through the list we also keep track of the largest memory map
        //entry, which we'll remove a portion of to use for pmm management data.
        //If more memory is added at runtime we can allocate new management data elsewhere,
        //but we have the benefit of operating in bulk during init.
        size_t selectedIndex = (size_t)-1;
        size_t regionCount = 0;
        size_t pageInfoSize = sizeof(PageInfo); //space for aligning the start of this allocator later on.
        for (size_t i = 0; i < mmapEntryCount; i++)
        {
            const limine_memmap_entry* entry = mmapEntries[i];
            if (entry->type != LIMINE_MEMMAP_USABLE && entry->type != LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE)
            {
                Log("Ignoring memmap entry: base=%#lx, length=%lx, type=%s", LogLevel::Verbose,
                    entry->base, entry->length, MemmapTypeStrs[entry->type]);
                continue;
            }

            pageInfoSize += (entry->length / PageSize) * sizeof(PageInfo);

            //if mmap region crosses a zone boundary, we'll need to allocate space for an extra PmRegion
            regionCount++;
            if (entry->base < 4 * GiB && entry->base + entry->length > 4 * GiB)
                regionCount++;

            //check if new region is larger than previously selected one
            if (selectedIndex == (size_t)-1 || mmapEntries[selectedIndex]->length < entry->length)
                selectedIndex = i;
        }
        
        size_t totalInitBytes = regionCount * sizeof(PmRegion) + pageInfoSize;
        if (selectedIndex == (size_t)-1 || mmapEntries[selectedIndex]->length < totalInitBytes)
            Log("PMM init failed: no region big enough for management data.", LogLevel::Fatal);

        auto conv = sl::ConvertUnits(totalInitBytes, sl::UnitBase::Binary);
        Log("PMM requires %lu.%lu%sB for management data. Allocating at 0x%lx, slack of %lu bytes.", LogLevel::Info, 
            conv.major, conv.minor, conv.prefix, mmapEntries[selectedIndex]->base, totalInitBytes % PageSize);

        //populate the pointers for the management data buffers
        totalInitBytes = sl::AlignUp(totalInitBytes, PageSize);
        regionAlloc = reinterpret_cast<PmRegion*>(mmapEntries[selectedIndex]->base + hhdmBase);
        remainingRegionAllocs = regionCount;
        
        pageInfoAlloc = reinterpret_cast<PageInfo*>(regionAlloc + remainingRegionAllocs);
        pageInfoAlloc = sl::AlignUp(pageInfoAlloc, sizeof(PageInfo));
        remainingPageInfoAllocs = pageInfoSize / sizeof(PageInfo);
        
        //and adjust the region we just carved out the buffers from
        mmapEntries[selectedIndex]->base += totalInitBytes;
        mmapEntries[selectedIndex]->length -= totalInitBytes;

        //add each usable region of the memory map to our own structures.
        for (size_t i = 0; i < mmapEntryCount; i++)
        {
            if (mmapEntries[i]->type != LIMINE_MEMMAP_USABLE)
                continue;
            
            IngestMemory(mmapEntries[i]->base, mmapEntries[i]->length);
        }

        conv = sl::ConvertUnits(zones[0].total * PageSize, sl::UnitBase::Binary);
        auto convHigh = sl::ConvertUnits(zones[1].total * PageSize, sl::UnitBase::Binary);
        Log("PMM init finished: highZone=%lu.%lu%sB, lowZone=%lu.%lu%sB.", LogLevel::Info, 
            convHigh.major, convHigh.minor, convHigh.prefix, conv.major, conv.minor, conv.prefix);
    }

    void PMM::IngestMemory(uintptr_t base, size_t length)
    {
        if (base < 4 * GiB)
        {
            size_t runover = 0;
            if (base + length > 4 * GiB)
            {
                runover = (base + length) - (4 * GiB);
                length -= runover;
            }

            InsertRegion(zones[0], base, length);
            if (runover > 0)
                InsertRegion(zones[1], base + length, runover);
        }
        else
            InsertRegion(zones[1], base, length);
    }

    PageInfo* PMM::Lookup(uintptr_t pfn)
    {
        PmZone* zone = nullptr;

        for (size_t i = 0; i < PmZonesCount; i++)
        {
            if (pfn < zones[i].head->base)
                continue;
            if (pfn >= zones[i].tail->base + zones[i].tail->totalPages * PageSize)
                continue;
            zone = &zones[i];
            break;
        }
        
        if (zone == nullptr)
            return nullptr;

        for (PmRegion* region = zone->head; region != nullptr; region = region->next)
        {
            if (pfn < region->base || pfn >= region->base + region->totalPages * PageSize)
                continue;
            
            const size_t index = (pfn - region->base) / sizeof(PageInfo);
            return &region->infoBuffer[index];
        }

        Log("Failed to get page info for physical address 0x%lx", LogLevel::Error, pfn);
        return nullptr;
    }

    uintptr_t PMM::AllocLow(size_t count)
    {
        if (count == 0)
            return 0;
        
        uintptr_t where = 0;
        PmRegion* region = zones[0].head;
        while (region != nullptr)
        {
            where = RegionAlloc(*region, count);
            if (where != 0)
            {
                zones[0].totalUsed.Add(count, sl::Relaxed);
                return where;
            }
            region = region->next;
        }

        ASSERT_UNREACHABLE(); //completely failed to allocate
    }

    uintptr_t PMM::Alloc(size_t count)
    {
        if (count == 0)
            return 0;
        
        uintptr_t where = 0;
        PmRegion* region = zones[1].head;
        while (region != nullptr)
        {
            where = RegionAlloc(*region, count);
            if (where != 0)
            {
                zones[1].totalUsed.Add(count, sl::Relaxed);
                return where;
            }
            region = region->next;
        }
        return AllocLow(count);
    }

    void PMM::Free(uintptr_t base, size_t count)
    {
        if ((base & 0xFFF) != 0)
        {
            Log("Misaligned PMM free at address 0x%016lx.", LogLevel::Warning, base);
            base &= ~0xFFFul;
        }

        if (base == 0 || count == 0)
            return;
        
        PmZone& zone = (base >= 4 * GiB) ? zones[1] : zones[0];
        PmRegion* searchStart = zone.head;
        while (searchStart != nullptr)
        {
            const uintptr_t regionTop = searchStart->base + searchStart->totalPages * PageSize;
            if (base < searchStart->base)
                break; //we've gone too far.
            if (base > regionTop)
            {
                searchStart = searchStart->next;
                continue; //but not far enough.
            }
            
            sl::ScopedLock regionLock(searchStart->lock);
            const size_t index = (base - searchStart->base) / PageSize;
            for (size_t i = 0; i < count; i++)
            {
                if (!searchStart->infoBuffer[index + i].flags.HasBits(PmFlags::Used))
                    Log("Double free attempted in PMM at address 0x%016lx.", LogLevel::Error, base + (i + index) * PageSize);
                else
                    searchStart->infoBuffer[index + i].flags.ClearBits(PmFlags::Used);
            }
            
            zone.totalUsed.Sub(count, sl::Relaxed);
            return;
        }

        Log("Cannot free %lu physical pages at %016lx", LogLevel::Error, count, base);
    }
}
