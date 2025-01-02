#pragma once

#include <Types.h>

extern "C"
{
    void* malloc(size_t length);
    void free(void* ptr, size_t length);
}

namespace sl
{
    struct DefaultAllocator
    {
        constexpr DefaultAllocator()
        {}
        
        [[nodiscard]]
        inline void* Allocate(size_t length)
        {
            return malloc(length);
        }

        inline void Deallocate(void* ptr, size_t length)
        {
            free(ptr, length);
        }
    };
}
