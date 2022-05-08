#include <memory/PhysicalMemory.h>
#include <Log.h>
#include <Memory.h>
#include <Platform.h>
#include <Utilities.h>
#include <stdint.h>
#include <Maths.h>
#include <Locks.h>

namespace Kernel::Memory
{
    /*  Notes about current implementation:
            - Memory map is assumed to be sorted, and have non-overlapping areas.
            - We create our own copy of any relevent details, mmap is not needed after init.
            - We dont support any operations pages across region boundaries (for now).
        
        Improvements to be made:
            - Logging address on errors, we'd need to check if dynamic memory exists so we can use Logf or not regular Log
            - Always searching from root region seems like a nice place for an optimization
    */
    
    PhysicalMemoryAllocator globalPma;
    PhysicalMemoryAllocator* PhysicalMemoryAllocator::Global()
    { return &globalPma; }

    PhysMemoryRegion* PhysicalMemoryAllocator::InitRegion(stivale2_mmap_entry* mmapEntry)
    {
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

    void PhysicalMemoryAllocator::Init(stivale2_struct_tag_memmap* mmap)
    {
        stats.usedPages = stats.totalPages = stats.kernelPages = stats.reclaimablePages = stats.reservedBytes = 0;
        
        //determine how much space is needed to manage this memory, and find the biggest region
        allocBufferSize = 0;
        size_t biggestRegionIndex = (size_t)-1;

        for (size_t i = 0; i < mmap->entries; i++)
        {
            stivale2_mmap_entry* currentEntry = &mmap->memmap[i];

            if (currentEntry->type == STIVALE2_MMAP_USABLE)
            {
                size_t bitmapLength = currentEntry->length / PAGE_FRAME_SIZE / 8 + 1;
                allocBufferSize += (sizeof(PhysMemoryRegion) * 2) + bitmapLength;

                if (biggestRegionIndex == (size_t)-1)
                    biggestRegionIndex = i;
                if (mmap->memmap[biggestRegionIndex].length < currentEntry->length)
                    biggestRegionIndex = i;
            }
        }

        if (mmap->memmap[biggestRegionIndex].length < allocBufferSize)
        {
            Log("Could not complete physical memory init, no region big enough to hold bitmaps.", LogSeverity::Fatal);
            return;
        }
        else
            Log("PMM attempting to bootstrap space for physical bitmaps.", LogSeverity::Info);
        
        //adjust our selected region to not include our alloc buffer
        const size_t allocBufferPages = allocBufferSize / PAGE_FRAME_SIZE + 1;
        allocBuffer = mmap->memmap[biggestRegionIndex].base;
        mmap->memmap[biggestRegionIndex].base += allocBufferPages * PAGE_FRAME_SIZE;
        mmap->memmap[biggestRegionIndex].length -= allocBufferPages * PAGE_FRAME_SIZE;

        //create structs to manage each area of physical memory
        rootRegion = nullptr;
        PhysMemoryRegion* prevRegion = nullptr;
        for (size_t i = 0; i < mmap->entries; i++)
        {
            if (mmap->memmap[i].type == STIVALE2_MMAP_USABLE)
            {
                PhysMemoryRegion* region = EnsureHigherHalfAddr(InitRegion(&mmap->memmap[i]));
                
                if (rootRegion == nullptr)
                    rootRegion = region;
                if (prevRegion != nullptr)
                    prevRegion->next = region;
                prevRegion = region;

                stats.totalPages += region->freePages;
            }

            //nothing interesting happening here, just keeping track of stuff for fun stats
            switch (mmap->memmap[i].type)
            {
            case STIVALE2_MMAP_RESERVED:
            case STIVALE2_MMAP_BAD_MEMORY:
                stats.reservedBytes += mmap->memmap[i].length;
                break;
            case STIVALE2_MMAP_ACPI_RECLAIMABLE: //acpi reclaimable dosnt need to be page-aligned, but often is.
            case STIVALE2_MMAP_BOOTLOADER_RECLAIMABLE:
                stats.reclaimablePages += mmap->memmap[i].length / PAGE_FRAME_SIZE;
                break;
            case STIVALE2_MMAP_KERNEL_AND_MODULES:
                stats.kernelPages += mmap->memmap[i].length / PAGE_FRAME_SIZE;
                stats.totalPages += mmap->memmap[i].length / PAGE_FRAME_SIZE; //these are placed in ram, so they're part of the total, even if we can never allocate them
                break;
            }
        }

        //we'll want to reserve the pages at 0x44000 + 0x45000 for AP trampoline code + data
        LockPages(AP_BOOTSTRAP_BASE, 2);

        Log("PMM successfully finished early init.", LogSeverity::Info);
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
