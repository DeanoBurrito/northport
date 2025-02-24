#pragma once

#include <arch/__Select.h>
#include <Time.h>

namespace Npk
{
    using TimerNanos = uint64_t;

    struct TimerCapabilities
    {
        bool timestampForUptime;
    };

    void GetTimeCapabilities(TimerCapabilities& caps);
    void InitLocalTimers();

    void SetAlarm(TimerNanos nanos);
    TimerNanos AlarmMax();
    TimerNanos GetTimestamp();
}

#ifdef NPK_ARCH_INCLUDE_TIMERS
#include NPK_ARCH_INCLUDE_TIMERS
#endif
