#pragma once

#include <stddef.h>
#include <stdint.h>
#include <memory/Slab.h>
#include <memory/Pool.h>

namespace Npk::Memory
{
    constexpr size_t SlabCount = 5;
    constexpr size_t PinnedSlabCount = 3;
    constexpr size_t SlabBaseSize = 32;

    class VirtualMemoryManager;
    
    class Heap
    {
    friend VirtualMemoryManager;
    private:
        uintptr_t nextSlabBase;
        SlabAlloc slabs[SlabCount];
        SlabAlloc pinnedSlabs[PinnedSlabCount];
        
        PoolAlloc pool;
        PoolAlloc pinnedPool;

        void Init();

    public:
        static Heap& Global();

        Heap() = default;
        Heap(const Heap&) = delete;
        Heap& operator=(const Heap&) = delete;
        Heap(Heap&&) = delete;
        Heap& operator=(Heap&&) = delete;

        void* Alloc(size_t, bool pinned = false);
        void Free(void* ptr, bool pinned = false);
    };

    struct PinnedAllocator
    {
        [[nodiscard]]
        inline void* Allocate(size_t length)
        { return Heap::Global().Alloc(length, true); }

        inline void Deallocate(void* ptr, size_t length)
        { Heap::Global().Free(ptr, true); (void)length; }
    };
}
