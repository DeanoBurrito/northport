#include <heap/UserSlab.h>
#include <heap/UserHeap.h>
#include <SyscallFunctions.h>
#include <Utilities.h>
#include <Logging.h>
#include <Memory.h>
#include <Locks.h>

namespace np::Userland
{
    void UserSlab::Init(sl::NativePtr base, size_t blockSize, size_t blockCount, sl::NativePtr debugBase)
    {
        debugAllocBase = debugBase;
        blocks = blockCount;
        this->blockSize = blockSize;

        const size_t bitmapBytes = blockCount / 8 + 1;
        const size_t bitmapBlocks = bitmapBytes / blockSize + 1;
        blockCount += bitmapBlocks;

        const size_t mapPages = blockCount * blockSize / UserHeapPageSize + 1;
        Syscall::MapMemory(base.raw, mapPages * UserHeapPageSize, Syscall::MemoryMapFlags::Writable);

        bitmapBase = base.As<uint8_t>();
        allocRegion = { base.raw + bitmapBlocks * blockSize, mapPages * UserHeapPageSize };
        allocRegion.length -= bitmapBlocks * blockSize;

        if (debugAllocBase.ptr != nullptr) //the alloc region is much bigger when using page-heap
            allocRegion = { debugBase, blocks * UserHeapPageSize * 2 };
        
        sl::memset(bitmapBase, 0, bitmapBytes);
        sl::SpinlockRelease(&lock);
    }
    
    void* UserSlab::Alloc()
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
            
            //NOTE: this assumes that no other ranges are mapped within the debug region - AllocRange may interfere with this.
            //TODO: place a NeverAllocate range directly after this one, that's why we added it!
            const size_t mappedPage = debugAllocBase.raw + (i * UserHeapPageSize * 2);
            Syscall::MapMemory(mappedPage, UserHeapPageSize, Syscall::MemoryMapFlags::Writable);
            return sl::NativePtr(mappedPage + UserHeapPageSize - blockSize).ptr;
        }

        Syscall::Log("User slab failed to allocate.", LogLevel::Error);
        return nullptr;
    }

    bool UserSlab::Free(sl::NativePtr where)
    {
        sl::ScopedSpinlock scopeLock(&lock);

        if (where.raw < allocRegion.base.raw || where.raw + blockSize > allocRegion.base.raw + allocRegion.length)
            return false;

        size_t whereIndex;
        if (debugAllocBase.ptr != nullptr)
            whereIndex = (where.raw - debugAllocBase.raw) / UserHeapPageSize / 2;
        else
            whereIndex = (where.raw - allocRegion.base.raw) / blockSize;
        
        if (!sl::BitmapGet(bitmapBase, whereIndex))
        {
            Syscall::Log("Attempted to free uslab memory already marked as free.", LogLevel::Error);
            return true; //memory was within bounds, but double freed
        }

        sl::BitmapClear(bitmapBase, whereIndex);
        return true;
    }
}
