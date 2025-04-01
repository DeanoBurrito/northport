#pragma once

#include <Defs.hpp>

namespace Npk
{
    void Panic(sl::StringSpan message);

    enum class LogLevel
    {
        Error,
        Warning,
        Info,
        Verbose,
        Trace,
        Debug,
    };

    SL_PRINTF_FUNC(1, 3)
    void Log(const char* msg, LogLevel level, ...);

    struct LogSink
    {
        sl::ListHook listHook;

        void (*Reset)();
        void (*Write)(sl::StringSpan message, LogLevel level);
    };

    void AddLogSink(LogSink& sink);
    void RemoveLogSink(LogSink& sink);
}

#define NPK_ASSERT(cond) do { } while(false)
#define NPK_UNREACHABLE() do { NPK_ASSERT(false); SL_UNREACHABLE(); } while (false)
#define NPK_CHECK(cond, ret) do { } while (false)
