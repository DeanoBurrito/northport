#pragma once

#include <stddef.h>
#include <BufferView.h>

namespace np::Userland
{
    class UserSlab
    {
    private:
        uint8_t* bitmapBase;
        size_t blocks;
        size_t blockSize;
        sl::BufferView allocRegion;
        char lock;
        sl::NativePtr debugAllocBase;

    public:
        UserSlab() = default;
        void Init(sl::NativePtr base, size_t blockSize, size_t blockCount, sl::NativePtr debugBase = nullptr);
        
        void* Alloc();
        bool Free(sl::NativePtr where);

        [[gnu::always_inline]] inline
        sl::BufferView Region() const
        { return allocRegion; }

        [[gnu::always_inline]] inline
        size_t SlabSize() const
        { return blockSize; }
    };
}
