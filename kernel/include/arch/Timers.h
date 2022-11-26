#pragma once

#include <stddef.h>
#include <Optional.h>

namespace Npk
{
    //These are helper functions for use by platform-specific implemenations.
    //You don't need to implement these yourself :) 
    sl::Opt<size_t> TryCalibrateTimer(const char* timerName, void (*Start)(), void (*Stop)(), size_t (*ReadTicks)());
    sl::Opt<size_t> CoalesceTimerRuns(long* timerRuns, size_t runCount, size_t allowedFails);
    //---
    
    void InitTimers();

    void SetSysTimer(size_t nanoseconds, void (*callback)(size_t));
    void PolledSleep(size_t nanoseconds);
    size_t PollTimer();
    size_t PolledTicksToNanos(size_t ticks);
    
    const char* SysTimerName();
    const char* PollTimerName();
}
