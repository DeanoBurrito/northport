#pragma once

#include <lib/Span.hpp>
#include <lib/Optional.hpp>

namespace Npk
{
    void InitRefTimers(uintptr_t& virtBase);
    void AcquireRefTimersLock();
    void ReleaseRefTimersLock();
    uint64_t RefTimersSleep(uint64_t nanos);

    sl::Opt<uint64_t> CoalesceTimerData(sl::Span<uint64_t> runs, 
        size_t allowedOutliers);
}
