#pragma once

#include <stddef.h>

namespace Npk
{
    void InitTimers();
    void SetTimer(size_t nanoseconds, void (*callback)(size_t));
    const char* ActiveTimerName();
}
