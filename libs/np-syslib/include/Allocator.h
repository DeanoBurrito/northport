#pragma once

#include <stddef.h>

extern "C"
{
    void* malloc(size_t length);
    void free(void* ptr);
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
            (void)length;
            free(ptr);
        }
    };
}
