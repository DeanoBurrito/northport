#pragma once

#include <Types.hpp>

namespace sl
{
    constexpr size_t NoLimit = -1;

    void* MemCopy(void* dest, const void* src, size_t len);
    void* MemSet(void* dest, int value, size_t len);
    void* MemMove(void* dest, const void* src, size_t len);
    int MemCompare(const void* lhs, const void* rhs, size_t len);
    size_t MemFind(const void* buff, uint8_t target, size_t limit);
}

