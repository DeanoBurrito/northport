#pragma once

#include <arch/Platform.h>
#include <stddef.h>
#include <containers/List.h>
#include <Locks.h>
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

    struct ClockQueue
    {
        sl::RunLevelLock<RunLevel::Clock> lock;
        sl::IntrFwdList<ClockEvent> events;
        size_t modifiedTicks;
    };

    void StartSystemClock();
    void QueueClockEvent(ClockEvent* event);
    void DequeueClockEvent(ClockEvent* event);
    sl::ScaledTime GetUptime();
}
