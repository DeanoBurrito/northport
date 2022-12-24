#include <memory/Pmm.h>
#include <arch/Platform.h>
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

    void PMM::InsertRegion(PmRegion** head, PmRegion** tail, uintptr_t base, size_t length)
    {
        ASSERT(base % PageSize == 0, "PMRegion base not page-aligned.");
        ASSERT(length % PageSize == 0, "PMRegion length not page-aligned.");
        
        //create the region
        ASSERT(metaBufferSize >= sizeof(PmRegion) * 2, "PMM metabuffer not big enough for new region, TODO:");
        PmRegion* latest = new(sl::AlignUp(metaBuffer, sizeof(PmRegion))) PmRegion{};
        metaBuffer += sizeof(PmRegion) * 2; //TODO: separate alloc buffers, then we can slab PmRegions, and not waste alignment space.
        metaBufferSize -= sizeof(PmRegion) * 2;

        sl::ScopedLock latestLock(latest->lock);
        latest->base = base;
        latest->freePages = latest->totalPages = length / PageSize;
        latest->bitmapHint = 0;

        //init the bitmap
        const size_t bitmapBytes = latest->totalPages / 8 + 1;
        ASSERT(metaBufferSize >= bitmapBytes, "PMM metabuffer not big enough for new region bitmap, TODO:");
        latest->bitmap = metaBuffer;
        metaBuffer += bitmapBytes;
        metaBufferSize -= bitmapBytes;
        sl::memset(latest->bitmap, 0, bitmapBytes);

        //find it's place in the list
        PmRegion* next = *head;
        PmRegion* prev = nullptr;
        while (next != nullptr && next->base < base)
        {
            prev = next;
            next = next->next;
        }

        if (next == nullptr)
        {
            if ((*tail) != nullptr)
            {
                sl::ScopedLock scopeLock((*tail)->lock);
                (*tail)->next = latest;
            }
            *tail = latest;
            if (*head == nullptr)
                *head = latest;
        }
        else if (prev == nullptr)
        {
            latest->next = *head;
            *head = latest;
        }
        else
        {
            latest->next = next;
            prev->next = latest;
        }

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
        zoneLow = zoneHigh = lowTail = highTail = nullptr;
        counts.totalHigh = counts.totalLow = counts.usedHigh = counts.usedLow = 0;
        metaBufferSize = 0;

        const size_t mmapEntryCount = Boot::memmapRequest.response->entry_count;
        limine_memmap_entry** const mmapEntries = Boot::memmapRequest.response->entries;

        size_t metaRegionIndex = (size_t)-1;
        for (size_t i = 0; i < mmapEntryCount; i++)
        {
            const limine_memmap_entry* entry = mmapEntries[i];

            if (entry->type != LIMINE_MEMMAP_USABLE && entry->type != LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE)
            {
                Log("Ignoring memmap entry: base=%#lx, length=%lx, type=%lu (%s)", LogLevel::Verbose,
                    entry->base, entry->length, entry->type, MemmapTypeStrs[entry->type]);
                continue;
            }

            //calculate size needed for bitmap and size needed (including alignment-slack) for struct.
            metaBufferSize += entry->length / PageSize / 8 + 1;
            metaBufferSize += 2 * sizeof(PmRegion);
            if (entry->base < 4 * GiB && entry->base + entry->length > 4 * GiB)
                metaBufferSize += 2 * sizeof(PmRegion); //we'll alloc regions in both low/high zones.

            if (metaRegionIndex == (size_t)-1 || mmapEntries[metaRegionIndex]->length < entry->length)
                metaRegionIndex = i;
        }

        //for this to happen, the memory map would need to be incredibly small or fragmented. This runs fine even on 128MiB.
        if (metaRegionIndex == (size_t)-1 || mmapEntries[metaRegionIndex]->length < metaBufferSize)
            Log("PMM init failed: no region big enough for bitmap + management structures.", LogLevel::Fatal);
        
        auto conv = sl::ConvertUnits(metaBufferSize, sl::UnitBase::Binary);
        Log("PMM requires %lu.%lu%sB for management data. Allocating at %#lx, slack of %lu bytes.", LogLevel::Info, 
            conv.major, conv.minor, conv.prefix, mmapEntries[metaRegionIndex]->base, metaBufferSize % PageSize);

        //take the space we need from the biggest region, aligning to page boundaries
        metaBufferSize = sl::AlignUp(metaBufferSize, PageSize);
        metaBuffer = reinterpret_cast<uint8_t*>(mmapEntries[metaRegionIndex]->base + hhdmBase);
        mmapEntries[metaRegionIndex]->base += metaBufferSize;
        mmapEntries[metaRegionIndex]->length -= metaBufferSize;

        for (size_t i = 0; i < mmapEntryCount; i++)
        {
            if (mmapEntries[i]->type != LIMINE_MEMMAP_USABLE)
                continue;
            
            IngestMemory(mmapEntries[i]->base, mmapEntries[i]->length);
        }

        conv = sl::ConvertUnits(counts.totalLow * PageSize, sl::UnitBase::Binary);
        auto convHigh = sl::ConvertUnits(counts.totalHigh * PageSize, sl::UnitBase::Binary);
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

            InsertRegion(&zoneLow, &lowTail, base, length);
            if (runover > 0)
                InsertRegion(&zoneHigh, &highTail, base + length, runover);

            counts.totalLow += length / PageSize;
            counts.totalHigh += runover / PageSize;
        }
        else
        {
            InsertRegion(&zoneHigh, &highTail, base, length);
            counts.totalHigh += length / PageSize;
        }
    }

    void PMM::DumpState()
    {
        //TODO: return structured data for parsing later, instead of writing to the log directly.
        auto convUsed = sl::ConvertUnits(counts.usedLow * PageSize, sl::UnitBase::Binary);
        auto convTotal = sl::ConvertUnits(counts.totalLow * PageSize, sl::UnitBase::Binary);
        Log("PMM state: low zone %lu.%lu%sB/%lu.%lu%sB used.", LogLevel::Debug, 
            convUsed.major, convUsed.minor, convTotal.prefix,
            convTotal.major, convTotal.minor, convTotal.prefix);
        
        size_t count = 0;
        for (PmRegion* it = zoneLow; it != nullptr; it = it->next)
        {
            convUsed = sl::ConvertUnits((it->totalPages - it->freePages) * PageSize, sl::UnitBase::Binary);
            convTotal = sl::ConvertUnits(it->totalPages * PageSize, sl::UnitBase::Binary);
            Log("l%lu: %lu.%lu%sB/%lu.%lu%sB in use, base=0x%lx, length=0x%lx, hint=%lu", LogLevel::Debug,
                count, convUsed.major, convUsed.minor, convUsed.prefix, convTotal.major, convTotal.minor, convTotal.prefix,
                it->base, it->totalPages * PageSize, it->bitmapHint);
            count++;
        }

        convUsed = sl::ConvertUnits(counts.usedHigh * PageSize, sl::UnitBase::Binary);
        convTotal = sl::ConvertUnits(counts.totalHigh * PageSize, sl::UnitBase::Binary);
        Log("PMM state: high zone %lu.%lu%sB/%lu.%lu%sB used.", LogLevel::Debug, 
            convUsed.major, convUsed.minor, convTotal.prefix,
            convTotal.major, convTotal.minor, convTotal.prefix);
        
        count = 0;
        for (PmRegion* it = zoneHigh; it != nullptr; it = it->next)
        {
            convUsed = sl::ConvertUnits((it->totalPages - it->freePages) * PageSize, sl::UnitBase::Binary);
            convTotal = sl::ConvertUnits(it->totalPages * PageSize, sl::UnitBase::Binary);
            Log("h%lu: %lu.%lu%sB/%lu.%lu%sB in use, base=0x%lx, length=0x%lx, hint=%lu", LogLevel::Debug,
                count, convUsed.major, convUsed.minor, convUsed.prefix, convTotal.major, convTotal.minor, convTotal.prefix,
                it->base, it->totalPages * PageSize, it->bitmapHint);
            count++;
        }
    }

    uintptr_t PMM::AllocLow(size_t count)
    {
        if (count == 0)
            return 0;
        
        uintptr_t where = 0;
        PmRegion* region = zoneLow;
        while (region != nullptr)
        {
            where = RegionAlloc(*region, count);
            if (where != 0)
            {
                __atomic_add_fetch(&counts.usedLow, count, __ATOMIC_RELAXED);
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
        PmRegion* region = zoneHigh;
        while (region != nullptr)
        {
            where = RegionAlloc(*region, count);
            if (where != 0)
            {
                __atomic_add_fetch(&counts.usedHigh, count, __ATOMIC_RELAXED);
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
        
        PmRegion* searchStart = (base >= 4 * GiB) ? zoneHigh : zoneLow;
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
            
            //we dont care when the count is modified, as long as its done atomically.
            __atomic_sub_fetch(base >= 4 * GiB ? &counts.usedHigh : &counts.usedLow, count, __ATOMIC_RELAXED);
            return;
        }

        Log("Cannot free %lu physical pages at %016lx", LogLevel::Error, count, base);
    }
}
