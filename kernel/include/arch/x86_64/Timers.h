#pragma once

#include <stddef.h>

namespace Npk
{
    void InitTimers();
    void InitInterruptTimers();

    void PolledSleep(size_t nanos);
    void InterruptSleep(size_t nanos, void (*callback)(size_t));
    
    const char* ActiveTimerName();
}
