#pragma once

#include <stddef.h>
#include <stdint.h>

namespace Npk::Memory
{
    constexpr size_t SlabCount = 5;
    constexpr size_t SlabBaseSize = 64;
    
    class Heap
    {
    private:
        uintptr_t nextSlabBase;

    public:
        static Heap& Global();

        void* Alloc(size_t);
        void Free(void* ptr);
    };
}
