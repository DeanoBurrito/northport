#pragma once

#include <Types.hpp>

inline void* operator new(size_t size, void* p) noexcept
{
    (void)size;
    return p;
}

inline void* operator new[](size_t size, void* p) noexcept
{
    (void)size;
    return p;
}

inline void operator delete(void* ignored, void* alsoIgnored) noexcept
{
    (void)ignored;
    (void)alsoIgnored;
}

inline void operator delete[](void* ignored, void* alsoIgnored) noexcept
{
    (void)ignored;
    (void)alsoIgnored;
}
