#pragma once

namespace Npk::Debug
{
    enum class LogLevel : unsigned long
    {
        Fatal = 0,
        Error = 1,
        Warning = 2,
        Info = 3,
        Verbose = 4,
        Debug = 5,
    };

    [[gnu::format(printf, 1, 3)]]
    void Log(const char* str, LogLevel level, ...);
}

using Npk::Debug::LogLevel;
using Npk::Debug::Log;

#define ASSERT(cond, msg) \
{ \
    if (!(cond)) \
        Log("Assert failed (%s:%u): %s", LogLevel::Fatal, __FILE__, __LINE__, (msg)); \
}

#define ASSERT_UNREACHABLE() \
{ \
    Log("Assert failed (%s:%u): ASSERT_UNREACHABLE reached.", LogLevel::Fatal, __FILE__, __LINE__); \
    __builtin_unreachable(); \
}
