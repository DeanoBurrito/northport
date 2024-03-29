#pragma once

#include <stdint.h>
#include <stddef.h>

namespace dl
{
    enum class LogLevel : size_t
    {
        Fatal = 0,
        Error = 1,
        Warning = 2,
        Info = 3,
        Verbose = 4,
        Debug = 5
    };

    [[gnu::format(printf, 1, 3)]]
    void Log(const char* str, LogLevel level, ...);
}

using dl::Log;
using dl::LogLevel;

#define ASSERT(cond, msg) \
{ \
    if (!(cond)) \
        Log("Assert failed (%s:%u): %s", LogLevel::Fatal, __FILE__, __LINE__, (msg)); \
}

#define ASSERT_(cond) \
{ \
    if (!(cond)) \
        Log("Assert failed (%s:%u): " #cond, LogLevel::Fatal, __FILE__, __LINE__); \
}

#define ASSERT_UNREACHABLE() \
{ \
    Log("Assert failed (%s:%u): ASSERT_UNREACHABLE reached.", LogLevel::Fatal, __FILE__, __LINE__); \
    __builtin_unreachable(); \
}

#define VALIDATE(cond, retval, msg) \
{ \
    if (!(cond)) \
    { \
        Log("Check failed (%s:%u): %s", LogLevel::Error, __FILE__, __LINE__, (msg)); \
        return retval; \
    } \
}

#define VALIDATE_(cond, retval) \
{ \
    if (!(cond)) \
    { \
        Log("Check failed (%s:%u): " #cond, LogLevel::Error, __FILE__, __LINE__); \
        return retval; \
    } \
}
