#pragma once

#include <stddef.h>

namespace Npk
{
    void InitTimers();
    void InitInterruptTimers();
    const char* ActiveTimerName();
    void PolledSleep(size_t nanos);
    void InterruptSleep(size_t nanos, void (*callback)(size_t));
    size_t GetTimerNanos();
}
