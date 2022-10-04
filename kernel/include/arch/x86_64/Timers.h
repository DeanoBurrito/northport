#pragma once

#include <stddef.h>

namespace Npk
{
    void InitTimers();
    const char* ActiveTimerName();
    void PolledSleep(size_t nanos);
    size_t GetTimerNanos();
}
