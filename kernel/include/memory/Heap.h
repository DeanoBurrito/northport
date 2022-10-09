#pragma once

#include <stddef.h>
#include <stdint.h>
#include <memory/Slab.h>
#include <memory/Pool.h>

namespace Npk::Memory
{
    constexpr size_t SlabCount = 5;
    constexpr size_t SlabBaseSize = 32;

    class VirtualMemoryManager;
    
    class Heap
    {
    friend VirtualMemoryManager;
    private:
        uintptr_t nextSlabBase;
        SlabAlloc slabs[SlabCount];
        PoolAlloc pool;

        void Init();

    public:
        static Heap& Global();

        Heap() = default;
        Heap(const Heap&) = delete;
        Heap& operator=(const Heap&) = delete;
        Heap(Heap&&) = delete;
        Heap& operator=(Heap&&) = delete;

        void* Alloc(size_t);
        void Free(void* ptr);
    };
}
