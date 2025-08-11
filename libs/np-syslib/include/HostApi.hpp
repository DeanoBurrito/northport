#pragma once

namespace sl
{
    struct TimePoint;
    struct CalendarPoint;
}

#define SL_EMIT_ERROR_HERE() \
    do \
    { \
        SlHostEmitError("Syslib error: %s:%i", __FILE__, __LINE__); \
    } while (false);

#define SL_EMIT_ERROR(what, ...) \
    do \
    { \
        SlHostEmitError("Syslib error: %s:%i" what, __FILE__, __LINE__, \
            ## __VA_ARGS__); \
    } while (false);

bool SlHostGetCurrentTime(sl::TimePoint& now);
bool SlHostGetCurrentDate(sl::CalendarPoint& now);
void SlHostEmitError(const char* str, ...);
