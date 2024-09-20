#pragma once

#include <stddef.h>
#include <PlacementNew.h>

namespace Npk::Core
{
    void InitLocalHeapCache();

    void* WiredAlloc(size_t size); //will return nullptr if allocation fails.
    void WiredFree(void* ptr, size_t size);
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
