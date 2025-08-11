#pragma once

#include <Compiler.hpp>
#include <Types.hpp>
#include <Span.hpp>
#include <Optional.hpp>

namespace Npk
{
    void InitReferenceTimers(uintptr_t& virtBase);
    uint64_t ReferenceSleep(uint64_t nanos);
    sl::Opt<uint64_t> CoalesceTimerData(sl::Span<uint64_t> runs, size_t allowedOutliers);

    bool CalibrateTsc();
    uint64_t MyTscFrequency();

    SL_ALWAYS_INLINE
    uint64_t ReadTsc()
    {
        uint64_t low;
        uint64_t high;
        asm volatile("lfence; rdtsc" : "=a"(low), "=d"(high) :: "memory");
        return low | (high << 32);
    }
}
