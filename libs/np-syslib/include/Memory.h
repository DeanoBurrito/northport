#pragma once

#include <Types.h>
#include <PlacementNew.h>

namespace sl
{
    template<typename T>
    void memsetT(void* const start, T value, size_t valueCount)
    {
        T* const si = reinterpret_cast<T* const>(start);
        
        for (size_t i = 0; i < valueCount; i++)
            si[i] = value;
    }

    void* memset(void* const start, uint8_t val, size_t count);

    void* memcopy(const void* const source, void* const destination, size_t count);
    void* memcopy(const void* const source, size_t sourceOffset, void* const destination, size_t destOffset, size_t count);

    int memcmp(const void* const a, const void* const b, size_t count);
    int memcmp(const void* const a, size_t offsetA, const void* const b, size_t offsetB, size_t count);

    size_t memfirst(const void* const buff, uint8_t target, size_t upperLimit);
    size_t memfirst(const void* const buff, size_t offset, uint8_t target, size_t upperLimit);
}

//These MUST be provided by the program, we'll forward declare them here to make them available.
extern "C"
{
    void* malloc(size_t length);
    void free(void* ptr, size_t length);

    //clang requires these to exist for __builtin_xyz, while GCC provides its own.
    void* memcpy(void* dest, const void* src, size_t len);
    void* memset(void* dest, int value, size_t len);
    void* memmove(void* dest, const void* src, size_t len);
}
