#pragma once

#include <arch/Timers.h>

namespace Npk
{
    void CalibrationTimersInit();
    TimerTickNanos CalibrationSleep(TimerTickNanos nanos);
}
