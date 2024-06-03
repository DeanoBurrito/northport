#include <arch/Timers.h>
#include <debug/Log.h>

namespace Npk
{
    void InitTimers()
    {}

    void SetSysTimer(size_t nanoseconds, bool (*callback)(void*))
    {
        ASSERT_UNREACHABLE();
    }

    size_t SysTimerMaxNanos()
    {
        ASSERT_UNREACHABLE();
    }

    void PolledSleep(size_t nanoseconds)
    {
        ASSERT_UNREACHABLE();
    }

    size_t PollTimer()
    {
        ASSERT_UNREACHABLE();
    }

    size_t PolledTicksToNanos(size_t ticks)
    {
        ASSERT_UNREACHABLE();
    }
    
    const char* SysTimerName()
    {
        ASSERT_UNREACHABLE();
    }

    const char* PollTimerName()
    {
        ASSERT_UNREACHABLE();
    }
}
