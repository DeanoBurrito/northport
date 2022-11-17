#pragma once

#include <stddef.h>
#include <Optional.h>

namespace Npk
{
    //This function is a helper function, and does not need to be implemented.
    sl::Opt<size_t> TryCalibrateTimer(const char* timerName, void (*PolledSleep)(size_t nanos), void (*Start)(), void (*Stop)(), size_t (*ReadTicks)());
    
    void InitTimers();

    void SetSysTimer(size_t nanoseconds, void (*callback)(size_t));
    void PolledSleep(size_t nanoseconds);
    size_t PollTimer();
    size_t PolledTicksToNanos(size_t ticks);
    
    const char* SysTimerName();
    const char* PollTimerName();
}
