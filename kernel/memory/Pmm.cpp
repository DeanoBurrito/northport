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
    
    PmRegion* PhysicalMemoryManager::AppendRegion(PmRegion* tail, uintptr_t baseAddr, size_t sizeBytes)
    {
        const uintptr_t alignedBase = sl::AlignUp(baseAddr, PageSize);
        const uintptr_t alignedSize = sl::AlignDown(sizeBytes, PageSize);
        PmRegion* region = new (sl::AlignUp(metaBuffer, sizeof(PmRegion))) PmRegion{};
        metaBuffer += sizeof(PmRegion) * 2;
        metaBufferSize -= sizeof(PmRegion) * 2;

        region->base = alignedBase;
        region->totalPages = region->freePages = alignedSize / PageSize;
        region->next = nullptr;
        region->bitmapHint = 0;

        const size_t bitmapBytes = region->totalPages / 8 + 1;
        region->bitmap = metaBuffer;
        metaBuffer += bitmapBytes;
        metaBufferSize -= bitmapBytes;
        sl::memset(region->bitmap, 0, bitmapBytes);

        if (tail != nullptr)
            tail->next = region;

        auto conversion = sl::ConvertUnits(region->totalPages * PageSize, sl::UnitBase::Binary);
        Log("PMRegion added: base=0x%08lx, pages=%lu (%lu.%lu%sB).", LogLevel::Info, region->base, region->totalPages,
            conversion.major, conversion.minor, conversion.prefix);
        return region;
    }

    uintptr_t PhysicalMemoryManager::RegionAlloc(PmRegion& region, size_t count)
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
    
    PhysicalMemoryManager globalPmm;
    PhysicalMemoryManager& PhysicalMemoryManager::Global()
    { return globalPmm; }
    
    void PhysicalMemoryManager::Init()
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

            if (entry->type != LIMINE_MEMMAP_USABLE)
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

    void PhysicalMemoryManager::IngestMemory(uintptr_t base, size_t length)
    {
        base = sl::AlignUp(base, PageSize);
        length = sl::AlignDown(length, PageSize);
        
        if (base < 4 * GiB)
        {
            size_t runover = 0;
            if (base + length > 4 * GiB)
            {
                runover = (base + length) - (4 * GiB);
                length -= runover;
            }

            lowTail = AppendRegion(lowTail, base, length);
            if (runover > 0)
                highTail = AppendRegion(highTail, base + length, runover);
            
            counts.totalLow += length / PageSize;
            counts.totalHigh += runover / PageSize;
        }
        else
        {
            highTail = AppendRegion(highTail, base, length);
            counts.totalHigh += length / PageSize;
        }

        if (zoneLow == nullptr)
            zoneLow = lowTail;
        if (zoneHigh == nullptr)
            zoneHigh = highTail;
    }

    uintptr_t PhysicalMemoryManager::AllocLow(size_t count)
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

        Log("PMM failed to allocate memory, boom.", LogLevel::Fatal);
        ASSERT_UNREACHABLE();
    }

    uintptr_t PhysicalMemoryManager::Alloc(size_t count)
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

    void PhysicalMemoryManager::Free(uintptr_t base, size_t count)
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
                continue; //but not far enough.
            
            sl::ScopedLock regionLock(searchStart->lock);
            const size_t index = (base - searchStart->base) / PageSize;
            for (size_t i = 0; i < count; i++)
            {
                if (!sl::BitmapClear(searchStart->bitmap, index + i))
                    Log("Double free attempted in PMM at address 0x%016lx.", LogLevel::Error, base + (i + index) * PageSize);
            }
            
            //we dont care when the count is modified, as long as its done atomically.
            __atomic_sub_fetch(base >= 4 * GiB ? &counts.usedHigh : &counts.usedLow, count, __ATOMIC_RELAXED);
        }

        Log("Cannot free %lu physical pages at %016lx", LogLevel::Error, count, base);
    }
}
