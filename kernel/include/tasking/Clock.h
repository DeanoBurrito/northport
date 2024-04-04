#pragma once

#include <arch/Platform.h>
#include <stddef.h>
#include <containers/List.h>
#include <Time.h>

namespace Npk::Tasking
{
    struct ClockQueue;

    struct ClockEvent
    {
        ClockEvent* next;
        ClockQueue* queue;

        DpcStore* dpc;
        sl::ScaledTime duration;
        size_t callbackCore;

        ClockEvent() : next(nullptr), dpc(nullptr), callbackCore(NoCoreAffinity)
        {}
    };

    void StartSystemClock();
    void QueueClockEvent(ClockEvent* event);
    void DequeueClockEvent(ClockEvent* event);
    sl::ScaledTime GetUptime();
}
