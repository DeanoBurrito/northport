#include <memory/PhysicalMemory.h>
#include <Log.h>
#include <Memory.h>
#include <Platform.h>
#include <stdint.h>

namespace Kernel::Memory
{
    /*  Notes about current implementation:
            - Memory map is assumed to be sorted, and have non-overlapping areas.
            - We create our own copy of any relevent details, mmap is not needed after init.
            = We dont suppot locking/unlocking pages across region boundaries.
            - Currently only supports allocating single pages.
        
        Improvements to be made:
            - Logging address on errors ,we'd need to check if dynamic memory exists so we can use Logf or not regular Log
            - Always searching from root region seems like a nice optimization
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
        region->bitmap = allocBuffer.As<uint8_t>();

        const size_t bitmapBytes = region->pageCount / 8 + 1;
        allocBuffer.raw += bitmapBytes;
        sl::memset(region->bitmap, 0, bitmapBytes);

        return region;
    }

    void PhysicalMemoryAllocator::Init(stivale2_struct_tag_memmap* mmap)
    {
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
                PhysMemoryRegion* region = InitRegion(&mmap->memmap[i]);
                
                if (rootRegion == nullptr)
                    rootRegion = region;
                if (prevRegion != nullptr)
                    prevRegion->next = region;
                prevRegion = region;
            }
        }

        Log("PMM successfully finished early init.", LogSeverity::Info);
    }

    void PhysicalMemoryAllocator::InitLate()
    {}

    FORCE_INLINE bool BitmapGet(uint8_t* base, size_t index)
    {
        const size_t byteIndex = index / 8;
        const size_t bitIndex = index % 8;
        return (*sl::NativePtr(base).As<uint8_t>(byteIndex) & (1 << bitIndex)) != 0;
    }

    FORCE_INLINE void BitmapSet(uint8_t* base, size_t index)
    {
        const size_t byteIndex = index / 8;
        const size_t bitIndex = index % 8;
        *sl::NativePtr(base).As<uint8_t>(byteIndex) |= (1 << bitIndex);
    }

    FORCE_INLINE void BitmapClear(uint8_t* base, size_t index)
    {
        const size_t byteIndex = index / 8;
        const size_t bitIndex = index % 8;
        *sl::NativePtr(base).As<uint8_t>(byteIndex) &= ~(1 << bitIndex);
    }

    void PhysicalMemoryAllocator::LockPage(sl::NativePtr address)
    { LockPages(address, 1); }

    void PhysicalMemoryAllocator::LockPages(sl::NativePtr lowestAddress, size_t count)
    {
        const size_t highestAddress = lowestAddress.raw + count * PAGE_FRAME_SIZE;
        
        for (PhysMemoryRegion* region = rootRegion; region != nullptr; region = region->next)
        {
            SpinlockAcquire(&region->lock);
            
            if (lowestAddress.raw >= region->baseAddress.raw && highestAddress <= region->baseAddress.raw + region->freePages * PAGE_FRAME_SIZE)
            {
                const size_t localIndex = (lowestAddress.raw - region->baseAddress.raw) / PAGE_FRAME_SIZE;

                if (count > region->pageCount - localIndex)
                {
                    count = region->pageCount - localIndex;
                    Log("Attempted to lock pages outside of PMM memory map.", LogSeverity::Warning);
                }

                for (size_t i = 0; i < count; i++)
                    BitmapSet(region->bitmap, localIndex + i);
                region->freePages -= count;
            }

            SpinlockRelease(&region->lock);
        }
    }

    void PhysicalMemoryAllocator::UnlockPage(sl::NativePtr address)
    { UnlockPages(address, 1); }

    void PhysicalMemoryAllocator::UnlockPages(sl::NativePtr lowestAddress, size_t count)
    {
        const size_t highestAddress = lowestAddress.raw + count * PAGE_FRAME_SIZE;
        
        for (PhysMemoryRegion* region = rootRegion; region != nullptr; region = region->next)
        {
            SpinlockAcquire(&region->lock);
            
            if (lowestAddress.raw >= region->baseAddress.raw && highestAddress <= region->baseAddress.raw + region->freePages * PAGE_FRAME_SIZE)
            {
                const size_t localIndex = (lowestAddress.raw - region->baseAddress.raw) / PAGE_FRAME_SIZE;

                if (count > region->pageCount - localIndex)
                {
                    count = region->pageCount - localIndex;
                    Log("Attempted to lock pages outside of PMM memory map.", LogSeverity::Warning);
                }

                for (size_t i = 0; i < count; i++)
                    BitmapClear(region->bitmap, localIndex + i);
                region->freePages += count;
            }

            SpinlockRelease(&region->lock);
        }
    }

    void* PhysicalMemoryAllocator::AllocPage()
    { 
        for (PhysMemoryRegion* region = rootRegion; region != nullptr; region = region->next)
        {
            if (region->freePages == 0)
                continue;

            SpinlockAcquire(&region->lock);

            for (size_t i = region->bitmapNextAlloc; i < region->pageCount; i++)
            {
                if (!BitmapGet(region->bitmap, i))
                {
                    BitmapSet(region->bitmap, i);

                    region->bitmapNextAlloc++;
                    if (region->bitmapNextAlloc == region->pageCount)
                        region->bitmapNextAlloc = 0;

                    region->freePages--;
                    
                    SpinlockRelease(&region->lock);
                    return sl::NativePtr(region->baseAddress.raw + i * PAGE_FRAME_SIZE).ptr;
                }

                if (region->bitmapNextAlloc > 0 && i + 1 == region->pageCount)
                    i = (size_t)-1;
            }

            SpinlockRelease(&region->lock);
        }
        
        Log("Failed to allocate a physical page.", LogSeverity::Error);
        return nullptr;
    }

    void PhysicalMemoryAllocator::FreePage(void* address)
    {
        sl::NativePtr addrPtr = address;
        addrPtr.raw -= addrPtr.raw % PAGE_FRAME_SIZE; //ensure that address is actually page aligned. TODO: modulo can be expensive, is it necessary?

        for (PhysMemoryRegion* region = rootRegion; region != nullptr; region = region->next)
        {
            size_t regionTopAddress = region->baseAddress.raw + region->pageCount * PAGE_FRAME_SIZE;
            if (addrPtr.raw >= region->baseAddress.raw && addrPtr.raw < regionTopAddress)
            {
                //its this region, clear the bit
                size_t bitmapIndex = (addrPtr.raw - region->baseAddress.raw) / PAGE_FRAME_SIZE;
                SpinlockAcquire(&region->lock);

                if (BitmapGet(region->bitmap, bitmapIndex))
                {
                    BitmapClear(region->bitmap, bitmapIndex);
                    region->freePages++;

                    if (bitmapIndex < region->bitmapNextAlloc)
                        region->bitmapNextAlloc = bitmapIndex;
                        
                    
                    SpinlockRelease(&region->lock);
                    return;
                }
                else
                {
                    SpinlockRelease(&region->lock);
                    Log("Failed to free a physical page, it was already free.", LogSeverity::Warning);
                    return;
                }

                SpinlockRelease(&region->lock);
            }
        }

        Log("Failed to free a physical page. Address was not found in memory regions.", LogSeverity::Error);
    }
}
