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
        sl::ScaledTime expiry;

        ClockEvent() : dpc(nullptr)
        {}
    };

    //setups the per-core clock queue, optionally starts counting system uptime
    //using the current core.
    void InitLocalClockQueue(bool startUptime);

    //called within the interrupt handler for the local interrupt clock,
    //by the arch layer.
    void ProcessLocalClock();
    
    //returns the uptime, using the configured resolution.
    sl::ScaledTime GetUptime();

    //queues a clock event to fire a dpc on this core, at some point in the future.
    void QueueClockEvent(ClockEvent* event);

    //attempts to prevent an existing clock event from expiring on this core.
    //Calling this function is a *request*, it does not guarentee the
    //dpc isnt fired, if you need to prevent an action occuring another
    //mechanism should be used.
    void DequeueClockEvent(ClockEvent* event);

}
