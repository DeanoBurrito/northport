#pragma once

#include <stddef.h>

namespace Npk
{
    struct TrapFrame; //see arch/Platform.h
}

namespace Npk::Debug
{
    enum class LogLevel : size_t
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

    using EarlyLogWrite = void (*)(const char* str, size_t length);
    void InitCoreLogBuffers();
    void AddEarlyLogOutput(EarlyLogWrite callback);
    void AttachLogDriver(size_t deviceId);
    void DetachLogDriver(size_t deviceId);

    void LogWriterServiceMain(void*); //service thread for push logs to drivers

    void Panic(TrapFrame* exceptionFrame, const char* reason);
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

#define VALIDATE(cond, retval, msg) \
{ \
    if (!(cond)) \
    { \
        Log("Check failed (%s:%u): %s", LogLevel::Error, __FILE__, __LINE__, (msg)); \
        return retval; \
    } \
}
