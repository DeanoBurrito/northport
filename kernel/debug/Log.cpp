#include <debug/Log.h>
#include <debug/LogBackends.h>
#include <debug/NanoPrintf.h>
#include <arch/Platform.h>
#include <tasking/Clock.h>
#include <tasking/Thread.h>
#include <Memory.h>
#include <Locks.h>

namespace Npk::Debug
{
    constexpr inline const char* LogLevelStrs[] = 
    {
        "[ Fatal ] ", "[ Error ] ", "[Warning] ",
        "[ Info  ] ", "[Verbose] ", "[ Debug ] "
    };

    constexpr inline const char* LogLevelAnsiStrs[] =
    {
        "\e[91m", "\e[31m", "\e[93m",
        "\e[97m", "\e[90m", "\e[94m",
    };

    constexpr inline const char* LogLevelAnsiReset = "\e[39m";
    constexpr inline size_t LogLevelStrLength = 10;
    constexpr inline size_t LogLevelAnsiLength = 5;

    constexpr inline const char* BackendStrings[] = 
    {
        "Terminal", "Debugcon", "NS16550"
    };

    uint8_t logBalloon[NP_LOG_BALLOON_SIZE]; //allocated in .bss
    size_t balloonHead = 0;

    sl::SpinLock outputLock;
    sl::SpinLock balloonLock;

    struct LogBackendStatus
    {
        bool enabled;
        bool (*Init)();
        void (*Write)(const char*, size_t);
    };

    LogBackendStatus backends[] = 
    {
        { false, InitTerminal, WriteTerminal },
        { false, nullptr, WriteDebugcon },
        { false, InitNs16550, WriteNs16550 },
    };
    size_t backendsAvailable = 0;

    inline void LogInternal(const char* str, size_t strLen, const char* timestamp, size_t timestampLen, LogLevel level)
    {
        for (size_t i = 0; i < (size_t)LogBackend::EnumCount; i++)
        {
            if (!backends[i].enabled)
                continue;

            backends[i].Write(timestamp, timestampLen);

            if ((size_t)level == -1ul)
            {
                backends[i].Write(str, strLen);
                continue;
            }

            backends[i].Write(LogLevelAnsiStrs[(size_t)level], LogLevelAnsiLength);
            backends[i].Write(LogLevelStrs[(size_t)level], LogLevelStrLength);
            backends[i].Write(LogLevelAnsiReset, LogLevelAnsiLength);
            backends[i].Write(str, strLen);
            backends[i].Write("\r\n", 2);
        }
    }

    void BalloonWrite(const char* message, size_t len)
    {
        if (balloonHead + len > NP_LOG_BALLOON_SIZE)
            return; //Not ideal, but there's bigger problems if we're overflowing the default size I think.
        
        sl::memcopy(message, logBalloon + balloonHead, len);
        balloonHead += len;
    }

    void DrainBalloon()
    {
        if (balloonHead == 0)
            return;
        
        InterruptGuard guard;
        sl::ScopedLock scopeLock(balloonLock);

        balloonHead = sl::Min(NP_LOG_BALLOON_SIZE - 1, balloonHead);
        logBalloon[balloonHead] = 0;
        //this is fine, the limine terminal handles upto 32K (balloon max size) characters pretty well (gj mint).
        LogInternal(reinterpret_cast<const char*>(logBalloon), balloonHead, nullptr, 0, (LogLevel)-1ul);
        balloonHead = 0;
    }
    
    void EnableLogBackend(LogBackend backend, bool enabled)
    {
        if (backend == LogBackend::EnumCount)
            return;
        if (backends[(size_t)backend].enabled == enabled)
            return;

        if (enabled && backends[(size_t)backend].Init != nullptr)
        {
            if (!backends[(size_t)backend].Init())
            {
                Log("Failed to init log backend: %s", LogLevel::Error, BackendStrings[(size_t)backend]);
                return;
            }
        }

        backends[(size_t)backend].enabled = enabled;
        backendsAvailable = enabled ? backendsAvailable + 1 : backendsAvailable - 1;
        Log("%s log backend: %s.", LogLevel::Info, enabled ? "Enabled" : "Disabled", BackendStrings[(size_t)backend]);
    }
    
    void Log(const char* message, LogLevel level, ...)
    {
        va_list argsList;
        va_start(argsList, level);
        const size_t strLen = npf_vsnprintf(nullptr, 0, message, argsList) + 1;
        va_end(argsList);
        char str[strLen];

        va_start(argsList, level);
        npf_vsnprintf(str, strLen, message, argsList);
        va_end(argsList);
        
        const size_t uptime = Tasking::GetUptime();
        const size_t coreId = CoreLocalAvailable() ? CoreLocal().id : 0;
        size_t threadId = 0;
        if (CoreLocalAvailable() && CoreLocal().schedThread != nullptr)
            threadId = static_cast<Tasking::Thread*>(CoreLocal().schedThread)->Id();

        const size_t timestampSize = npf_snprintf(nullptr, 0, "%lu.%03lu p%lut%lu ", uptime / 1000, uptime % 1000, coreId, threadId) + 1;
        char timestampStr[timestampSize];
        npf_snprintf(timestampStr, timestampSize, "%lu.%03lu p%lut%lu ", uptime / 1000, uptime % 1000, coreId, threadId);

        if (backendsAvailable > 0 && CoreLocalAvailable() && CoreLocal().runLevel == RunLevel::Normal)
        {
            //write directly to the outputs
            sl::ScopedLock scopeLock(outputLock);
            
            DrainBalloon();
            LogInternal(str, strLen, timestampStr, timestampSize, level);
        }
        else
        {
            //use the balloon
            sl::ScopedLock scopeLock(balloonLock);

            BalloonWrite(timestampStr, timestampSize - 1);
            BalloonWrite(LogLevelAnsiStrs[(size_t)level], LogLevelAnsiLength);
            BalloonWrite(LogLevelStrs[(size_t)level], LogLevelStrLength);
            BalloonWrite(LogLevelAnsiReset, LogLevelAnsiLength);
            BalloonWrite(str, strLen);
            BalloonWrite("\r\n", 2);
        }

        if (level == LogLevel::Fatal)
        {
            CoreLocal().runLevel = RunLevel::Normal;
            outputLock.Unlock(); //TODO: implement panic()
            Log("System has halted indefinitely.", LogLevel::Info);
            Halt();
        }
    }
}
