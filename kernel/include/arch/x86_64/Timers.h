#pragma once

#include <arch/Timers.h>

namespace Npk
{
    void CalibrationTimersInit();
    TimerNanos CalibrationSleep(TimerNanos nanos);
}
