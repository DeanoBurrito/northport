#pragma once

#include <stddef.h>

inline void* operator new(size_t size, void* p) noexcept
{
    return p;
}

inline void* operator new[](size_t size, void* p) noexcept
{
    return p;
}

inline void operator delete(void* ignored, void* alsoIgnored) noexcept
{}

inline void operator delete[](void* ignored, void* alsoIgnored) noexcept
{}