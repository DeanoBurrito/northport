#pragma once

#include <stddef.h>

namespace Npk::Tasking
{
    //Please don't call this manually, it's wired into the timer callback.
    void ClockEventDispatch(size_t);

    void StartSystemClock();
    void QueueClockEvent(size_t nanoseconds, void* payloadData, void (*callback)(void* data), bool periodic = false);
    size_t GetUptime();
}
