#include <memory/KernelSlab.h>
#include <memory/VirtualMemory.h>
#include <Memory.h>
#include <Locks.h>
#include <Log.h>

namespace Kernel::Memory
{
    void KernelSlab::Init(sl::NativePtr base, size_t blockSize, size_t blockCount, sl::NativePtr debugBase)
    {
        debugAllocBase = debugBase;
        blocks = blockCount;
        this->blockSize = blockSize;

        const size_t bitmapBytes = blockCount / 8 + 1;
        const size_t bitmapBlocks = bitmapBytes / bitmapBlocks + 1;
        blockCount += bitmapBlocks;

        const size_t mapPages = blockCount * blockSize / PAGE_FRAME_SIZE + 1;
        PageTableManager::Current()->MapRange(base, mapPages, MemoryMapFlags::AllowWrites);

        bitmapBase = base.As<uint8_t>();
        allocRegion = { base.raw + bitmapBlocks * blockSize, mapPages * PAGE_FRAME_SIZE };
        allocRegion.length -= bitmapBlocks * blockSize;

        if (debugAllocBase.ptr != nullptr) //the alloc region is much bigger when using page-heap
            allocRegion = { debugBase, blocks * PAGE_FRAME_SIZE * 2 };
        
        sl::memset(bitmapBase, 0, bitmapBytes);
        sl::SpinlockRelease(&lock);
    }
    
    void* KernelSlab::Alloc()
    {
        sl::ScopedSpinlock scopeLock(&lock);

        for (size_t i = 0; i < blocks; i++)
        {
            if (bitmapBase[i / 8] == 0xFF)
            {
                i += 7;
                continue; //we can skip an entire byte if all bits are set
            }

            if (sl::BitmapGet(bitmapBase, i))
                continue;

            sl::BitmapSet(bitmapBase, i);

            if (debugAllocBase.ptr == nullptr)
                return sl::NativePtr(i * blockSize + allocRegion.base.raw).ptr;
            
            /*
                The idea was inspired by a blogpost about microsoft's page heap. The basic idea is we allocate exactly the number
                of bytes requested, right below an unmapped page. Therefore any attemps to write beyond the end of the buffer
                trigger a page fault. If the debug virtual address base is null, the feature is disabled (normal speedy operation).
            */
            const size_t mappedPage = debugAllocBase.raw + (i * PAGE_FRAME_SIZE * 2);
            PageTableManager::Current()->MapMemory(mappedPage, MemoryMapFlags::AllowWrites);
            PageTableManager::Current()->UnmapMemory(mappedPage + PAGE_FRAME_SIZE);
            return sl::NativePtr(mappedPage + PAGE_FRAME_SIZE - blockSize).ptr;
        }

        Log("Kernel slab failed to allocate.", LogSeverity::Error);
        return nullptr;
    }

    bool KernelSlab::Free(sl::NativePtr where)
    {
        sl::ScopedSpinlock scopeLock(&lock);

        if (where.raw < allocRegion.base.raw || where.raw + blockSize > allocRegion.base.raw + allocRegion.length)
            return false;

        size_t whereIndex;
        if (debugAllocBase.ptr != nullptr)
            whereIndex = (where.raw - debugAllocBase.raw) / PAGE_FRAME_SIZE / 2;
        else
            whereIndex = (where.raw - allocRegion.base.raw) / blockSize;
        
        if (!sl::BitmapGet(bitmapBase, whereIndex))
        {
            Log("Attempted to free kslab memory already marked as free.", LogSeverity::Error);
            return true; //memory was within bounds, but double freed
        }

        sl::BitmapClear(bitmapBase, whereIndex);
        return true;
    }

    void KernelSlab::GetStats(HeapSlabStats& stats) const
    {
        stats.slabSize = blockSize;
        stats.totalSlabs = blocks;
        stats.base = allocRegion.base;
        stats.segments = 1;

        stats.usedSlabs = 0;
        for (size_t i = 0; i < blocks; i++)
        {
            if (sl::BitmapGet(bitmapBase, i))
                stats.usedSlabs++;
        }
    }
}
