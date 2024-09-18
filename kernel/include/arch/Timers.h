#pragma once

#include <arch/__Select.h>
#include <stddef.h>
#include <Optional.h>

namespace Npk
{
    //discover and init system-wide timers, called exactly once on the BSP. This is called after
    //APs have started (with no synchronization between them however) and after the BSP has
    //run InitLocalTimers().
    void InitGlobalTimers();

    //discover and init per-cpu timers, called once per cpu. If per-core interrupt and polling
    //timers are available, this function should populate the ClockQueue slot in the
    //core-local struct.
    void InitLocalTimers();

    //sets a timer to interrupt the current core after a number of nanoseconds.
    void ArmInterruptTimer(size_t nanoseconds, bool (*callback)(void*));

    //attempts to disarm an already running interrupt timer. Returns if disarm was successful or not.
    bool DisarmInterruptTimer();

    //returns the maximum duration the interrupt timer can be set for.
    size_t InterruptTimerMaxNanos();

    //returns a monotonically incrementing tick count for the current core.
    size_t PollTimer();

    //converts a value from PollTimer() to nanoseconds.
    size_t PollTicksToNanos(size_t ticks);
    
    //vanity functions: returns the name of the current interrupt and polling timers.
    const char* InterruptTimerName();
    const char* PollTimerName();
}

#ifdef NPK_ARCH_INCLUDE_TIMERS
#include NPK_ARCH_INCLUDE_TIMERS
#endif
