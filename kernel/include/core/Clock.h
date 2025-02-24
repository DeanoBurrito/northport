#pragma once

#include <core/RunLevels.h>
#include <Time.h>
#include <containers/List.h>

namespace Npk::Core
{
    struct ClockQueue;

    struct ClockEvent
    {
        sl::FwdListHook listHook;
        ClockQueue* queue;

        DpcStore* dpc;
        sl::TimeCount expiry;

        ClockEvent() : dpc(nullptr)
        {}
    };

    //setups the per-core clock queue, optionally starts counting system uptime
    //using the current core.
    void InitLocalClockQueue();

    //returns the uptime, using the configured resolution.
    sl::TimeCount GetUptime();

    //queues a clock event to fire a dpc on this core, at some point in the future.
    void QueueClockEvent(ClockEvent* event);

    //attempts to prevent an existing clock event from expiring on this core.
    //Calling this function is a *request*, it does not guarentee the
    //dpc isnt fired, if you need to prevent an action occuring another
    //mechanism should be used.
    //Returns whether the event was successfully removed or not
    bool DequeueClockEvent(ClockEvent* event);

}
