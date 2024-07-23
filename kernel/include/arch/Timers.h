#pragma once

#include <arch/__Select.h>
#include <stddef.h>
#include <Optional.h>

namespace Npk
{
    //These are helper functions for use by platform-specific implemenations.
    //You don't need to implement these yourself :) TODO: do we still need these?
    sl::Opt<size_t> CoalesceTimerRuns(long* timerRuns, size_t runCount, size_t allowedFails);
    //---
    
    void InitGlobalTimers();
    void InitLocalTimers();

    void SetSysTimer(size_t nanoseconds, bool (*callback)(void*));
    size_t SysTimerMaxNanos();
    size_t PollTimer();
    size_t PolledTicksToNanos(size_t ticks);
    
    const char* SysTimerName();
    const char* PollTimerName();
}

#ifdef NPK_ARCH_INCLUDE_TIMERS
#include NPK_ARCH_INCLUDE_TIMERS
#endif
