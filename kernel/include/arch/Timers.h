#pragma once

#include <arch/__Select.h>
#include <Time.h>

namespace Npk
{
    using TimerTickNanos = uint64_t;

    struct TimerCapabilities
    {
        bool pollSuitableForUptime;
    };

    void GetTimerCapabilities(TimerCapabilities& caps);
    void InitLocalTimers();

    bool ArmIntrTimer(TimerTickNanos nanos);
    TimerTickNanos MaxIntrTimerExpiry();
    TimerTickNanos ReadPollTimer();
}

#ifdef NPK_ARCH_INCLUDE_TIMERS
#include NPK_ARCH_INCLUDE_TIMERS
#endif
