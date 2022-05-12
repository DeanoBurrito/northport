#pragma once

#include <stddef.h>
#include <BufferView.h>

namespace Kernel::Memory
{
    struct HeapSlabStats
    {
        sl::NativePtr base;
        size_t totalSlabs;
        size_t usedSlabs;
        size_t segments;
        size_t slabSize;
    };
    
    class KernelSlab
    {
    private:
        uint8_t* bitmapBase;
        size_t blocks;
        size_t blockSize;
        sl::BufferView allocRegion;
        char lock;
        sl::NativePtr debugAllocBase;
        //TODO: would be nice to include a next pointer, so we could effectively expand these forever
        //TODO: would be nice to hint at where the next free slab is

    public:
        KernelSlab() = default;
        //debugBase being non-null will use page-heap allocation instead. 
        void Init(sl::NativePtr base, size_t blockSize, size_t blockCount, sl::NativePtr debugBase = nullptr);
        
        void* Alloc();
        bool Free(sl::NativePtr where);
        void GetStats(HeapSlabStats& stats) const;

        [[gnu::always_inline]] inline
        sl::BufferView Region() const
        { return allocRegion; }

        [[gnu::always_inline]] inline
        size_t SlabSize() const
        { return blockSize; }
    };
}
