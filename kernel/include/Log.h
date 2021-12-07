#pragma once

#include <stdint.h>

namespace Kernel
{
    enum class LogSeverity : uint8_t
    {
        //general log level.
        Info = 0,
        //potential issue, but not a serious fault.
        Warning = 1,
        //operation could not be completed, non-fatal issue.
        Error = 2,
        //usually causes kernel to panic, an unrecoverable issue.
        Fatal = 3,
        //extra debug info, can be safely ignored.
        Verbose = 4,

        //dont use this to log, its just a count entry.
        EnumCount
    };

    struct LogEntry
    {
        LogSeverity severity;
        uint64_t when;
        const char* message;
    };

    enum class LogDestination
    {
        DebugCon,
        FramebufferOverwrite,

        EnumCount
    };

    void LoggingInitEarly(); //gets basic logging capabilities going, no message log though
    void LoggingInitFull(); //initializes the logging buffer and other nice features that require the heap and virtual memory

    bool IsLogDestinationEnabled(LogDestination dest);
    void EnableLogDestinaton(LogDestination dest, bool enabled = true);

    void Log(const char* message, LogSeverity level);

    //NOTE: see Logf.cpp for implementation. I didnt want to pollute Log.cpp (as it can operate before pmm/vmm are available)
    void Logf(const char* formatStr, LogSeverity level, ...);
}
