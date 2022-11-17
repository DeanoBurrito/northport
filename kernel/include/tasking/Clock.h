#pragma once

#include <stddef.h>

namespace Npk::Tasking
{
    void StartSystemClock();
    void QueueClockEvent(size_t nanoseconds, void* payloadData, void (*callback)(void* data), bool periodic = false, size_t core = -1);
    size_t GetUptime();
}
