#pragma once

#include <stdint.h>

namespace Kernel
{
    enum class LogSeverity : uint8_t
    {
        Info = 0,
        Warning = 1,
        Error = 2,
        Fatal = 3,
        Verbose = 4,

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
    void LoggingInitFull(); //initializes the logging buffer and other nice features that require software setup

    bool IsLogDestinationEnabled(LogDestination dest);
    void EnableLogDestinaton(LogDestination dest, bool enabled = true);

    void Log(const char* message, LogSeverity level);
}
