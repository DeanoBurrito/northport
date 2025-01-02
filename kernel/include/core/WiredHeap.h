#pragma once

#include <stddef.h>
#include <PlacementNew.h>

namespace Npk::Core
{
    void InitWiredHeap();
    void InitLocalHeapCache();

    void* WiredAlloc(size_t size); //will return nullptr if allocation fails.
    void WiredFree(void* ptr, size_t size);

    struct WiredHeapAllocator
    {
        constexpr WiredHeapAllocator() {}

        [[nodiscard]]
        inline void* Allocate(size_t length)
        {
            return WiredAlloc(length);
        }

        inline void Deallocate(void* ptr, size_t length)
        {
            WiredFree(ptr, length);
        }
    };
}

template<typename T>
inline T* NewWired()
{ 
    void* ptr = Npk::Core::WiredAlloc(sizeof(T));
    if (ptr != nullptr)
        return new(ptr) T();
    return nullptr;
}

template<typename T>
inline void DeleteWired(T* obj)
{
    if (obj != nullptr)
        Npk::Core::WiredFree(obj, sizeof(T));
}
