#include <memory/PhysicalMemory.h>
#include <Log.h>
#include <Memory.h>
#include <Utilities.h>
#include <Locks.h>
#include <boot/Limine.h>

namespace Kernel::Memory
{
    /*  Notes about current implementation:
            - Memory map is assumed to be sorted, and have non-overlapping areas.
            - We create our own copy of any relevent details, mmap is not needed after init.
            - We dont support any operations pages across region boundaries (for now).
        
        Improvements to be made:
            - Always searching from root region seems like a nice place for an optimization
    */
    
    PhysicalMemoryAllocator globalPma;
    PhysicalMemoryAllocator* PhysicalMemoryAllocator::Global()
    { return &globalPma; }

    PhysMemoryRegion* PhysicalMemoryAllocator::InitRegion(void* entry)
    {
        limine_memmap_entry* mmapEntry = reinterpret_cast<limine_memmap_entry*>(entry);
        
        //align the address within the 2x space we allocated for it. Since it's UB to access an unaligned struct.
        const size_t alignBase = (allocBuffer.raw + sizeof(PhysMemoryRegion) - 1) / sizeof(PhysMemoryRegion);
        PhysMemoryRegion* region = reinterpret_cast<PhysMemoryRegion*>(alignBase * sizeof(PhysMemoryRegion));
        allocBuffer.raw += sizeof(PhysMemoryRegion) * 2;

        region->baseAddress = mmapEntry->base;
        region->pageCount = mmapEntry->length / PAGE_FRAME_SIZE;
        region->freePages = region->pageCount;
        region->next = nullptr;
        region->lock = 0;
        region->bitmapNextAlloc = 0;
        region->bitmap = EnsureHigherHalfAddr(allocBuffer.As<uint8_t>());

        const size_t bitmapBytes = region->pageCount / 8 + 1;
        allocBuffer.raw += bitmapBytes;
        sl::memset(region->bitmap, 0, bitmapBytes);

        return region;
    }


    void PhysicalMemoryAllocator::InitFromLimine()
    {
        stats.usedPages = stats.totalPages = stats.kernelPages = stats.reclaimablePages = stats.reservedBytes = 0;

        //first pass over memmap: determine how much space is needed for the individual bitmaps
        allocBufferSize = 0;
        size_t biggestRegionIndex = (size_t)-1;

        const limine_memmap_response* mmap = Boot::memmapRequest.response;
        for (size_t i = 0; i < mmap->entry_count; i++)
        {
            const limine_memmap_entry* currentEntry = mmap->entries[i];

            if (currentEntry->type == LIMINE_MEMMAP_USABLE)
            {
                size_t bitmapLength = currentEntry->length / PAGE_FRAME_SIZE / 8 + 1;
                allocBufferSize += (sizeof(PhysMemoryRegion) * 2) + bitmapLength;

                if (biggestRegionIndex == (size_t)-1)
                    biggestRegionIndex = i;
                if (mmap->entries[biggestRegionIndex]->length < currentEntry->length)
                    biggestRegionIndex = i;
            }
        }

        if (mmap->entries[biggestRegionIndex]->length < allocBufferSize)
            Log("Could not complete physical memory init, no region big enough to hold bitmaps.", LogSeverity::Fatal);
        else
            Log("PMM carving out space for bitmap from memory map.", LogSeverity::Info);

        //adjust the biggest regon, reserving the space we need for ourselves.
        const size_t allocBufferPages = allocBufferSize / PAGE_FRAME_SIZE + 1;
        allocBuffer = mmap->entries[biggestRegionIndex]->base;
        mmap->entries[biggestRegionIndex]->base += allocBufferPages * PAGE_FRAME_SIZE;
        mmap->entries[biggestRegionIndex]->length -= allocBufferPages * PAGE_FRAME_SIZE;
        
        //second pass: actually creating our own structs to manage the phys memory regions
        rootRegion = nullptr;
        PhysMemoryRegion* prevRegion = nullptr;

        for (size_t i = 0; i < mmap->entry_count; i++)
        {
            if (mmap->entries[i]->type == LIMINE_MEMMAP_USABLE)
            {
                PhysMemoryRegion* region = EnsureHigherHalfAddr(InitRegion(mmap->entries[i]));
                
                if (rootRegion == nullptr)
                    rootRegion = region;
                if (prevRegion != nullptr)
                    prevRegion->next = region;
                prevRegion = region;

                stats.totalPages += region->freePages;
            }

            //nothing interesting happening here, just keeping track of stuff for fun stats
            switch (mmap->entries[i]->type)
            {
            case LIMINE_MEMMAP_RESERVED:
            case LIMINE_MEMMAP_BAD_MEMORY:
                stats.reservedBytes += mmap->entries[i]->length;
                break;
            case LIMINE_MEMMAP_ACPI_RECLAIMABLE: //acpi reclaimable dosnt need to be page-aligned, but often is.
            case LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE:
                stats.reclaimablePages += mmap->entries[i]->length / PAGE_FRAME_SIZE;
                break;
            case LIMINE_MEMMAP_KERNEL_AND_MODULES:
                stats.kernelPages += mmap->entries[i]->length / PAGE_FRAME_SIZE;
                stats.totalPages += mmap->entries[i]->length / PAGE_FRAME_SIZE; //these are placed in ram, so they're part of the total, even if we can never allocate them
                break;
            }
        }

        //we'll want to reserve the pages at 0x44000 + 0x45000 for AP trampoline code + data
        LockPages(AP_BOOTSTRAP_BASE, 2);

        Log("PMM finishing creating bitmap.", LogSeverity::Info);
    }

    void PhysicalMemoryAllocator::LockPage(sl::NativePtr address)
    { LockPages(address, 1); }

    void PhysicalMemoryAllocator::LockPages(sl::NativePtr lowestAddress, size_t count)
    {
        const size_t highestAddress = lowestAddress.raw + count * PAGE_FRAME_SIZE;
        
        for (PhysMemoryRegion* region = rootRegion; region != nullptr; region = region->next)
        {
            sl::ScopedSpinlock scopeLock(&region->lock);
            
            if (lowestAddress.raw >= region->baseAddress.raw && highestAddress <= region->baseAddress.raw + region->freePages * PAGE_FRAME_SIZE)
            {
                const size_t localIndex = (lowestAddress.raw - region->baseAddress.raw) / PAGE_FRAME_SIZE;

                if (count > region->pageCount - localIndex)
                {
                    count = region->pageCount - localIndex;
                    Log("Attempted to lock pages outside of PMM memory map.", LogSeverity::Warning);
                }

                for (size_t i = 0; i < count; i++)
                    sl::BitmapSet(region->bitmap, localIndex + i);
                region->freePages -= count;
                stats.usedPages += count;
            }
        }
    }

    void PhysicalMemoryAllocator::UnlockPage(sl::NativePtr address)
    { UnlockPages(address, 1); }

    void PhysicalMemoryAllocator::UnlockPages(sl::NativePtr lowestAddress, size_t count)
    {
        const size_t highestAddress = lowestAddress.raw + count * PAGE_FRAME_SIZE;
        
        for (PhysMemoryRegion* region = rootRegion; region != nullptr; region = region->next)
        {
            sl::ScopedSpinlock scopeLock(&region->lock);
            
            if (lowestAddress.raw >= region->baseAddress.raw && highestAddress <= region->baseAddress.raw + region->freePages * PAGE_FRAME_SIZE)
            {
                const size_t localIndex = (lowestAddress.raw - region->baseAddress.raw) / PAGE_FRAME_SIZE;

                if (count > region->pageCount - localIndex)
                {
                    count = region->pageCount - localIndex;
                    Log("Attempted to lock pages outside of PMM memory map.", LogSeverity::Warning);
                }

                for (size_t i = 0; i < count; i++)
                    sl::BitmapClear(region->bitmap, localIndex + i);
                region->freePages += count;
                stats.usedPages -= count;
            }
        }
    }

    void* PhysicalMemoryAllocator::AllocPage()
    { return AllocPages(1); }

    void* PhysicalMemoryAllocator::AllocPages(size_t count)
    {
        auto TryAllocForCount = [&](PhysMemoryRegion* region, size_t& baseIndex)
        {
            if (baseIndex + count > region->pageCount)
            {
                baseIndex = (size_t)-1;
                return false;
            }
            
            for (size_t i = 0; i < count; i++)
            {
                if (!sl::BitmapGet(region->bitmap, baseIndex + i))
                    continue;
                
                baseIndex = baseIndex + i + 1;
                return false;
            }

            //there's enough space, claim it as used, update the stats and return success
            for (size_t i = 0; i < count; i++)
                sl::BitmapSet(region->bitmap, baseIndex + i);
            
            region->freePages -= count;
            stats.usedPages += count;
            region->bitmapNextAlloc += count;
            if (region->bitmapNextAlloc >= region->pageCount)
                region->bitmapNextAlloc = 0;
            return true;
        };

        for (PhysMemoryRegion* region = rootRegion; region != nullptr; region = region->next)
        {
            if (region->freePages < count)
                continue;
            if (region->bitmapNextAlloc + count > region->pageCount)
                continue;

            sl::ScopedSpinlock regionLock(&region->lock);

            size_t tryIndex = region->bitmapNextAlloc;
            while (tryIndex + count < region->pageCount && tryIndex != (size_t)-1)
            {
                if (TryAllocForCount(region, tryIndex))
                    return sl::NativePtr(region->baseAddress.raw).As<void>(tryIndex * PAGE_FRAME_SIZE);
            }
        }

        //PMM runs before we have dynamic memory, and logf() drops any calls before we have a heap setup.
        //therefore if we fail to allocate before we have a heap, we wouldnt see an error if we only used logf(), hence the double log call.
        Log("Failed to allocate physical pages.", LogSeverity::Error);
        Logf("PMM attempted to allocate %lu pages", LogSeverity::Error, count);
        return nullptr;
    }

    void PhysicalMemoryAllocator::FreePage(sl::NativePtr address)
    { FreePages(address, 1); }

    void PhysicalMemoryAllocator::FreePages(sl::NativePtr address, size_t count)
    {
        for (PhysMemoryRegion* region = rootRegion; region != nullptr; region = region->next)
        {
            size_t regionTopAddress = region->baseAddress.raw + region->pageCount * PAGE_FRAME_SIZE;
            if (address.raw >= region->baseAddress.raw && address.raw < regionTopAddress)
            {
                //it's this region, do a bounds check, then set the bits
                size_t bitmapIndex = (address.raw - region->baseAddress.raw) / PAGE_FRAME_SIZE;
                if (bitmapIndex + count > region->pageCount)
                    count = region->pageCount - bitmapIndex;

                sl::ScopedSpinlock regionLock(&region->lock);

                for (size_t i = 0; i < count; i++)
                {
                    if (sl::BitmapGet(region->bitmap, bitmapIndex + i))
                    {
                        sl::BitmapClear(region->bitmap, bitmapIndex + i);
                        region->freePages++;
                        stats.usedPages++;

                        if (bitmapIndex + i < region->bitmapNextAlloc)
                            region->bitmapNextAlloc = bitmapIndex + i;
                    }
                    else
                        Log("Failed to free a physical page, it was already free.", LogSeverity::Warning);
                }

                //since we only support operating on 1 region for now, early exit
                return;
            }
        }

        Log("Failed to free a physical page. Address was not found in memory regions.", LogSeverity::Error);
    }
}
