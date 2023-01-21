#include <memory/Pmm.h>
#include <boot/LimineTags.h>
#include <Memory.h>
#include <Bitmap.h>
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

        //allocate the bitmap and clear it
        const size_t bitmapBytes = ((length / PageSize) / 8) + 1;
        ASSERT(bitmapBytes <= bitmapFreeSize, "TODO: expand bitmap");
        uint8_t* regionBitmap = bitmapAlloc;
        bitmapFreeSize += bitmapBytes;
        sl::memset(regionBitmap, 0, bitmapBytes);

        //allocate the region, initialize it
        ASSERT(remainingRegionAllocs > 0, "PMRegion slab full. TODO: expand into new region.")
        PmRegion* latest = new(regionAlloc++) PmRegion(base, length, regionBitmap);
        remainingRegionAllocs -= 1;
        bitmapAlloc += bitmapBytes;
        bitmapFreeSize -= bitmapBytes;

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
        size_t start = region.bitmapHint;
        size_t end = region.totalPages;

    try_region_alloc_search:
        for (size_t i = start; i < end; i++)
        {
            if (i + count >= end)
                break;
            
            bool usable = true;
            for (size_t mod = 0; mod < count; mod++)
            {
                if (sl::BitmapGet(region.bitmap, i + mod))
                {
                    i += mod;
                    usable = false;
                    break;
                }
            }

            if (!usable)
                continue;
            
            for (size_t j = 0; j < count; j++)
                sl::BitmapSet(region.bitmap, i + j);
            
            region.freePages -= count;
            region.bitmapHint = (i + count) % region.totalPages;
            return region.base + (i * PageSize);
        }

        if (start == region.bitmapHint && region.bitmapHint != 0)
        {
            start = 0;
            end = region.bitmapHint;
            goto try_region_alloc_search;
        }
        return 0;
    }
    
    PMM globalPmm;
    PMM& PMM::Global()
    { return globalPmm; }
    
    void PMM::Init()
    {
        remainingRegionAllocs = 0;
        bitmapFreeSize = 0;
        new(&zones[0]) PmZone();
        new(&zones[1]) PmZone();

        const size_t mmapEntryCount = Boot::memmapRequest.response->entry_count;
        limine_memmap_entry** const mmapEntries = Boot::memmapRequest.response->entries;

        //scan the memory map and determine how many PMRegions are needed, and how large
        //the total bitmap size required is. We can expand this later at runtime if needed.
        size_t selectedIndex = (size_t)-1;
        size_t regionCount = 0;
        size_t totalBitmapSize = 0;
        for (size_t i = 0; i < mmapEntryCount; i++)
        {
            const limine_memmap_entry* entry = mmapEntries[i];
            if (entry->type != LIMINE_MEMMAP_USABLE && entry->type != LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE)
            {
                Log("Ignoring memmap entry: base=%#lx, length=%lx, type=%s", LogLevel::Verbose,
                    entry->base, entry->length, MemmapTypeStrs[entry->type]);
                continue;
            }

            regionCount++;
            totalBitmapSize += ((entry->length / PageSize) / 8) + 1;
            if (entry->base < 4 * GiB && entry->base + entry->length > 4 * GiB)
                regionCount++;

            if (selectedIndex == (size_t)-1 || mmapEntries[selectedIndex]->length < entry->length)
                selectedIndex = i;
        }
        
        size_t totalInitBytes = totalBitmapSize + regionCount * sizeof(PmRegion);
        if (selectedIndex == (size_t)-1 || mmapEntries[selectedIndex]->length < totalInitBytes)
            Log("PMM init failed: no region big enough for management data.", LogLevel::Fatal);
        //TODO: we could actually allocate this as two separate buffers if necessary, less restrictive.

        auto conv = sl::ConvertUnits(totalInitBytes, sl::UnitBase::Binary);
        Log("PMM requires %lu.%lu%sB for management data. Allocating at 0x%lx, slack of %lu bytes.", LogLevel::Info, 
            conv.major, conv.minor, conv.prefix, mmapEntries[selectedIndex]->base, totalInitBytes % PageSize);

        //populate buffer pointers
        totalInitBytes = sl::AlignUp(totalInitBytes, PageSize);
        regionAlloc = reinterpret_cast<PmRegion*>(mmapEntries[selectedIndex]->base + hhdmBase);
        remainingRegionAllocs = regionCount;
        bitmapAlloc = reinterpret_cast<uint8_t*>(regionAlloc + regionCount);
        bitmapFreeSize = totalInitBytes - (sizeof(PmRegion) * regionCount);
        
        //and adjust the region we just carved out the buffers from
        mmapEntries[selectedIndex]->base += totalInitBytes;
        mmapEntries[selectedIndex]->length -= totalInitBytes;

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
        
        PmZone& zone = (base >= 4 * GiB) ? zones[0] : zones[1];
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
                if (!sl::BitmapClear(searchStart->bitmap, index + i))
                    Log("Double free attempted in PMM at address 0x%016lx.", LogLevel::Error, base + (i + index) * PageSize);
            }
            
            zone.totalUsed.Sub(count, sl::Relaxed);
            return;
        }

        Log("Cannot free %lu physical pages at %016lx", LogLevel::Error, count, base);
    }
}
