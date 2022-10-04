#include <memory/Pmm.h>
#include <arch/Platform.h>
#include <boot/LimineTags.h>
#include <Memory.h>
#include <Bitmap.h>
#include <Maths.h>
#include <debug/Log.h>
#include <UnitConverter.h>

/*
    Improvements:
    - Memory zones: some areas of memory are precious, and should be allocated last (< 1MB, < 4GB).
    - Smarted allocations: allocate from smaller physical regions. Don't fragment a big block of
        physical pages to allocate 1 or 2 pages. Pull those from areas that can only allocate smaller blocks.
*/

namespace Npk::Memory
{
    constexpr const char* MemmapTypeStrs[] = 
    {
        "usable", "reserved", "acpi reclaim", "acpi nvs",
        "bad", "bootloader reclaim", "kernel/modules", "framebuffer"
    };
    
    void PhysicalMemoryManager::AppendRegion(uintptr_t baseAddr, size_t sizeBytes)
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

        if (head == nullptr)
            head = tail = region;
        else
        {
            tail->next = region;
            tail = region;
        }

        Log("PMRegion added: base=%#lx, pages=%lu.", LogLevel::Info, region->base, region->totalPages);
    }
    
    PhysicalMemoryManager globalPmm;
    PhysicalMemoryManager& PhysicalMemoryManager::Global()
    { return globalPmm; }
    
    void PhysicalMemoryManager::Init()
    {
        head = tail = nullptr;
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

            if (metaRegionIndex == (size_t)-1 || mmapEntries[metaRegionIndex]->length < entry->length)
                metaRegionIndex = i;
        }

        if (metaRegionIndex == (size_t)-1 || mmapEntries[metaRegionIndex]->length < metaBufferSize)
            Log("PMM init failed: no region big enough for bitmap + management structures.", LogLevel::Fatal);
        
        auto converted = sl::ConvertUnits(metaBufferSize, sl::UnitBase::Binary);
        Log("PMM requires %lu.%lu%sB for management data. Allocating at %#lx", LogLevel::Info, 
            converted.major, converted.minor, converted.prefix, mmapEntries[metaRegionIndex]->base);

        //take the space we need from the biggest region, aligning to page boundaries
        metaBufferSize = sl::AlignUp(metaBufferSize, PageSize);
        metaBuffer = reinterpret_cast<uint8_t*>(mmapEntries[metaRegionIndex]->base + hhdmBase);
        mmapEntries[metaRegionIndex]->base += metaBufferSize;
        mmapEntries[metaRegionIndex]->length -= metaBufferSize;

        size_t usableMemory = 0;
        for (size_t i = 0; i < mmapEntryCount; i++)
        {
            if (mmapEntries[i]->type != LIMINE_MEMMAP_USABLE)
                continue;
            usableMemory += mmapEntries[i]->length;
            AppendRegion(mmapEntries[i]->base, mmapEntries[i]->length);
        }

        converted = sl::ConvertUnits(usableMemory, sl::UnitBase::Binary);
        Log("PMM init finished: %lu.%lu%sB of usable physical memory.", LogLevel::Info,
            converted.major, converted.minor, converted.prefix);
    }

    void* PhysicalMemoryManager::Alloc(size_t count)
    {
        auto TryAllocWithin = [&](PMRegion& region, size_t start, size_t limit)
        {
            if (limit < count)
                return 0ul;
            
            for (size_t i = start; i < limit; i++)
            {
                bool canAlloc = true;
                for (size_t mod = 0; mod < count; mod++)
                {
                    if (sl::BitmapGet(region.bitmap, i + mod))
                    {
                        i += mod;
                        canAlloc = false;
                        break;
                    }
                }

                if (!canAlloc)
                    continue;
                
                for (size_t mod = 0; mod < count; mod++)
                    sl::BitmapSet(region.bitmap, i + mod);

                region.freePages -= count;
                region.bitmapHint = i + count;
                if (region.bitmapHint >= region.totalPages)
                    region.bitmapHint = 0;
                
                return region.base + i * PageSize;
            }

            return 0ul;
        };

        if (count == 0)
            return nullptr;
        
        for (PMRegion* region = head; region != nullptr; region = region->next)
        {
            sl::ScopedLock regionLock(region->lock);

            if (region->freePages < count)
                continue;
            
            uintptr_t alloc = TryAllocWithin(*region, region->bitmapHint, region->totalPages);
            if (alloc == 0ul && region->bitmapHint > 0)
                alloc = TryAllocWithin(*region, 0, region->bitmapHint);
            if (alloc > 0ul)
                return reinterpret_cast<void*>(alloc);
        }

        Log("PMM failed to allocate memory, boom.", LogLevel::Fatal);
        __builtin_unreachable();
    }

    void PhysicalMemoryManager::Free(void* base, size_t count)
    {
        if (base == nullptr || count == 0)
            return;
        
        for (PMRegion* region = head; region != nullptr; region = region->next)
        {
            if ((uintptr_t)base < region->base)
                break; //we went too far
            if ((uintptr_t)base > region->base + region->totalPages * PageSize)
                continue; //not far enough!
            
            sl::ScopedLock regionLock(region->lock);
            const size_t index = ((uintptr_t)base - region->base) / PageSize;
            for (size_t i = 0; i < count; i++)
            {
                if (!sl::BitmapClear(region->bitmap, index + i))
                    Log("Attempted to double free physical page: %#lx", LogLevel::Error, (uintptr_t)base + i * PageSize);
            }
            return;
        }

        Log("Cannot free %lu physical pages at %#lx, outside of all regions.", LogLevel::Error, count, (uintptr_t)base);
    }
}
