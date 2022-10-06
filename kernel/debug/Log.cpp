#include <debug/Log.h>
#include <debug/LogBackends.h>
#include <debug/NanoPrintf.h>
#include <arch/Platform.h>
#include <Memory.h>
#include <Maths.h>
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
        "Terminal", "Serial"
    };

    constexpr size_t LogBalloonSize = 0x8000;
    uint8_t logBalloon[LogBalloonSize]; //allocated in .bss
    size_t balloonHead = 0;

    sl::SpinLock outputLock;
    sl::SpinLock balloonLock;
    bool suppressLogOutput asm("suppressLogOutput") = false;

    bool backends[] = 
    {
        false,
        false
    };
    size_t backendsAvailable = 0;

    inline void LogInternal(const char* str, size_t strLen, const char* timestamp, size_t timestampLen, LogLevel level)
    {
        for (size_t i = 0; i < (size_t)LogBackend::EnumCount; i++)
        {
            if (!backends[i])
                continue;
            
            void (*Write)(const char*, size_t);
            switch ((LogBackend)i)
            {
            case LogBackend::Serial: Write = WriteSerial; break;
            case LogBackend::Terminal: Write = WriteTerminal; break;
            default: continue;
            }

            Write(timestamp, timestampLen);

            if ((size_t)level == -1ul)
            {
                Write(str, strLen);
                continue;
            }

            Write(LogLevelAnsiStrs[(size_t)level], LogLevelAnsiLength);
            Write(LogLevelStrs[(size_t)level], LogLevelStrLength);
            Write(LogLevelAnsiReset, LogLevelAnsiLength);
            Write(str, strLen);
            Write("\r\n", 2);
        }
    }

    void BalloonWrite(const char* message, size_t len)
    {
        if (balloonHead + len > LogBalloonSize)
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

        balloonHead = sl::Min(LogBalloonSize - 1, balloonHead);
        logBalloon[balloonHead] = 0;
        //this is fine, the limine terminal handles upto 32K (balloon max size) characters pretty well (gj mint).
        LogInternal(reinterpret_cast<const char*>(logBalloon), balloonHead, nullptr, 0, (LogLevel)-1ul);
        balloonHead = 0;
    }
    
    void EnableLogBackend(LogBackend backend, bool enabled)
    {
        if (backend == LogBackend::EnumCount)
            return;
        if (backends[(size_t)backend] == enabled)
            return;

        if (enabled)
        {
            switch (backend)
            {
            case LogBackend::Terminal: InitTerminal(); break;
            case LogBackend::Serial: InitSerial(); break;
            default: break;
            }
        }

        backends[(size_t)backend] = enabled;
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
        
        const size_t uptime = 0; //TODO: clock
        const size_t timestampSize = npf_snprintf(nullptr, 0, "%lu.%lu ", uptime / 1000, uptime % 1000) + 1;
        char timestampStr[timestampSize];
        npf_snprintf(timestampStr, timestampSize, "%lu.%lu ", uptime / 1000, uptime % 1000);

        if (backendsAvailable > 0 && !suppressLogOutput)
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
            suppressLogOutput = false;
            outputLock.Unlock();
            Log("System has halted indefinitely.", LogLevel::Info);
            Halt();
        }
    }
}
