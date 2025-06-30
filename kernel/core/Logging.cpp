#include <KernelApi.hpp>
#include <Memory.h>
#include <NanoPrintf.h>
#include <Maths.h>

namespace Npk
{
    constexpr size_t MaxLogLength = 128;

    IntrSpinLock logLock;
    sl::List<LogSink, &LogSink::listHook> logSinks; //TODO: locking?

    void Log(const char* message, LogLevel level, ...)
    {
        va_list args;
        va_start(args, level);
        char formatBuff[MaxLogLength];
        const size_t formatLen = npf_vsnprintf(formatBuff, MaxLogLength, message, args);
        va_end(args);

        const size_t realLen = sl::Min(formatLen, MaxLogLength);

        LogSinkMessage sinkMsg {};
        sinkMsg.level = level;
        sinkMsg.text = { formatBuff, realLen };
        sinkMsg.when = sl::TimePoint::Now();
        sinkMsg.who = "kernel";
        sinkMsg.cpu = MyCoreId();

        sl::ScopedLock scopeLock(logLock);
        for (auto it = logSinks.Begin(); it != logSinks.End(); ++it)
            it->Write(sinkMsg);
    }

    void AddLogSink(LogSink& sink)
    {
        logSinks.PushBack(&sink);

        if (sink.Reset != nullptr)
            sink.Reset();
    }

    void RemoveLogSink(LogSink& sink)
    {
        logSinks.Remove(&sink);
    }

    sl::StringSpan LogLevelStr(LogLevel level)
    {
        constexpr sl::StringSpan levelStrs[] =
        {
            "Error",
            "Warning",
            "Info",
            "Verbose",
            "Trace",
            "Debug",
        };

        if (static_cast<size_t>(level) > static_cast<size_t>(LogLevel::Debug))
            return "unknown level";
        return levelStrs[static_cast<size_t>(level)];
    }
    static_assert(static_cast<LogLevel>(0) == LogLevel::Error);
    static_assert(static_cast<LogLevel>(1) == LogLevel::Warning);
    static_assert(static_cast<LogLevel>(2) == LogLevel::Info);
    static_assert(static_cast<LogLevel>(3) == LogLevel::Verbose);
    static_assert(static_cast<LogLevel>(4) == LogLevel::Trace);
    static_assert(static_cast<LogLevel>(5) == LogLevel::Debug);
}
