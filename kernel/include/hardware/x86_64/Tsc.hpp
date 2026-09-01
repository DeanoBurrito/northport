#pragma once

#include <lib/Types.hpp>
#include <lib/Compiler.hpp>

namespace Npk
{
    void InitRefTimers(uintptr_t& virtBase);
    void InitTsc();
    uint64_t TscFrequency();

    SL_ALWAYS_INLINE
    uint64_t ReadTsc()
    {
        uint64_t low;
        uint64_t high;

        asm volatile("lfence; rdtsc" : "=a"(low), "=d"(high) :: "memory");

        return low | (high << 32);
    }
}
