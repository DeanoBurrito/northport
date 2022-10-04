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
    
    PMRegion* PhysicalMemoryManager::AppendRegion(PMRegion* tail, uintptr_t baseAddr, size_t sizeBytes)
    {
        const uintptr_t alignedBase = sl::AlignUp(baseAddr, PageSize);
        const uintptr_t alignedSize = sl::AlignDown(sizeBytes, PageSize);
        PMRegion* region = new (sl::AlignUp(metaBuffer, sizeof(PMRegion))) PMRegion{};
        metaBuffer += sizeof(PMRegion) * 2;

        region->base = alignedBase;
        region->totalPages = region->freePages = alignedSize / PageSize;
        region->next = nullptr;
        region->bitmapHint = 0;

        const size_t bitmapBytes = region->totalPages / 8 + 1;
        region->bitmap = metaBuffer;
        metaBuffer += bitmapBytes;
        sl::memset(region->bitmap, 0, bitmapBytes);

        if (tail != nullptr)
            tail->next = region;

        auto conversion = sl::ConvertUnits(region->totalPages * PageSize, sl::UnitBase::Binary);
        Log("PMRegion added: base=0x%08lx, pages=%lu (%lu.%lu%sB).", LogLevel::Info, region->base, region->totalPages,
            conversion.major, conversion.minor, conversion.prefix);
        return region;
    }

    uintptr_t PhysicalMemoryManager::RegionAlloc(PMRegion& region, size_t count)
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
        zoneLow = zoneHigh = nullptr;
        size_t metaBufferSize = 0;

        //calculate and create spaace for metabuffer
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
            metaBufferSize += 2 * sizeof(PMRegion);
            if (entry->base < 4 * GiB && entry->base + entry->length > 4 * GiB)
                metaBufferSize += 2 * sizeof(PMRegion); //we'll alloc regions in both low/high zones.

            if (metaRegionIndex == (size_t)-1 || mmapEntries[metaRegionIndex]->length < entry->length)
                metaRegionIndex = i;
        }

        if (metaRegionIndex == (size_t)-1 || mmapEntries[metaRegionIndex]->length < metaBufferSize)
            Log("PMM init failed: no region big enough for bitmap + management structures.", LogLevel::Fatal);
        
        auto conv = sl::ConvertUnits(metaBufferSize, sl::UnitBase::Binary);
        Log("PMM requires %lu.%lu%sB for management data. Allocating at %#lx", LogLevel::Info, 
            conv.major, conv.minor, conv.prefix, mmapEntries[metaRegionIndex]->base);

        //take the space we need from the biggest region, aligning to page boundaries
        metaBufferSize = sl::AlignUp(metaBufferSize, PageSize);
        metaBuffer = reinterpret_cast<uint8_t*>(mmapEntries[metaRegionIndex]->base + hhdmBase);
        mmapEntries[metaRegionIndex]->base += metaBufferSize;
        mmapEntries[metaRegionIndex]->length -= metaBufferSize;

        size_t lowZoneMemory = 0;
        size_t highZoneMemory = 0;
        PMRegion* lowTail = nullptr;
        PMRegion* highTail = nullptr;
        for (size_t i = 0; i < mmapEntryCount; i++)
        {
            if (mmapEntries[i]->type != LIMINE_MEMMAP_USABLE)
                continue;
            
            //Low zone is anything 32-bit addressable, high zone is anything above.
            if (mmapEntries[i]->base < 4 * GiB)
            {
                size_t runover = 0;
                if (mmapEntries[i]->base + mmapEntries[i]->length > 4 * GiB)
                {
                    runover = (mmapEntries[i]->base + mmapEntries[i]->length) - 4 * GiB;
                    mmapEntries[i]->length -= runover;
                }
                lowTail = AppendRegion(lowTail, mmapEntries[i]->base, mmapEntries[i]->length);
                if (runover > 0)
                    highTail = AppendRegion(highTail, mmapEntries[i]->base + mmapEntries[i]->length, runover);

                if (zoneLow == nullptr)
                    zoneLow = lowTail;
                if (zoneHigh == nullptr && runover > 0)
                    zoneHigh = highTail;

                lowZoneMemory += mmapEntries[i]->length;
                highZoneMemory += runover;
            }
            else
            {
                highTail = AppendRegion(highTail, mmapEntries[i]->base, mmapEntries[i]->length);
                if (zoneHigh == nullptr)
                    zoneHigh = highTail;
                
                highZoneMemory += mmapEntries[i]->length;
            }
        }

        conv = sl::ConvertUnits(lowZoneMemory, sl::UnitBase::Binary);
        auto convHigh = sl::ConvertUnits(highZoneMemory, sl::UnitBase::Binary);
        Log("PMM init finished: highZone=%lu.%lu%sB, lowZone=%lu.%lu%sB.", LogLevel::Info, 
            convHigh.major, convHigh.minor, convHigh.prefix, conv.major, conv.minor, conv.prefix);
    }

    uintptr_t PhysicalMemoryManager::AllocLow(size_t count)
    {
        if (count == 0)
            return 0;
        
        uintptr_t where = 0;
        PMRegion* region = zoneLow;
        while (region != nullptr)
        {
            where = RegionAlloc(*region, count);
            if (where != 0)
                return where;
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
        PMRegion* region = zoneHigh;
        while (region != nullptr)
        {
            where = RegionAlloc(*region, count);
            if (where != 0)
                return where;
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
        
        PMRegion* searchStart = (base >= 4 * GiB) ? zoneHigh : zoneLow;
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
        }

        Log("Cannot free %lu physical pages at %016lx", LogLevel::Error, count, base);
    }
}
