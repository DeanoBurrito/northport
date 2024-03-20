#pragma once

#include <arch/Platform.h>
#include <stddef.h>
#include <Time.h>

namespace Npk::Tasking
{
    struct ClockEvent
    {
        ClockEvent* next;

        DpcStore* dpc;
        size_t nanosRemaining;
        size_t callbackCore;

        ClockEvent() : next(nullptr), dpc(nullptr), callbackCore(NoCoreAffinity)
        {}
    };

    void StartSystemClock();
    void QueueClockEvent(ClockEvent* event);
    void DequeueClockEvent(ClockEvent* event);
    sl::ScaledTime GetUptime();
}
