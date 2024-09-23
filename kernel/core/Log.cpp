#include <core/Log.h>
#include <core/Clock.h>
#include <core/WiredHeap.h>
#include <Panic.h>
#include <Locks.h>
#include <Maths.h>
#include <NanoPrintf.h>
#include <Memory.h>

namespace Npk::Core
{
    constexpr sl::StringSpan PanicLockFailedStr = 
        "\r\n [!!] Failed to acquire log output lock - latest messages will be dropped! [!!] \r\n";
    constexpr sl::StringSpan EndLineStr = "\r\n";
    constexpr sl::StringSpan UptimeStr = "%zu.%03zu ";
    constexpr sl::StringSpan ProcThreadStr = "%zu%.1s%zu ";
    constexpr sl::StringSpan LogLevelStrs[] =
    {
        "\e[91m[ Fatal ]\e[39m ", 
        "\e[31m[ Error ]\e[39m ", 
        "\e[93m[Warning]\e[39m ", 
        "\e[97m[ Info  ]\e[39m ", 
        "\e[90m[Verbose]\e[39m ", 
        "\e[94m[ Debug ]\e[39m ",
    };

    constexpr size_t MaxLogOutputs = 4;
    constexpr size_t MaxLogLength = 128;

    const LogOutput* logOuts[MaxLogOutputs];
    size_t logOutCount = 0;
    sl::SpinLock logOutLock;

    constexpr sl::TimeScale logTimescale = sl::TimeScale::Millis;
    constexpr size_t LogDrainLimit = 64;
    constexpr size_t MaxAllocExhangeTries = 4;
    constexpr size_t MaxAllocAttempts = 4;
    constexpr size_t LogBufferSize = 0x8000;

    struct LogBuffer
    {
        char* buffer;
        size_t size;
        sl::Atomic<size_t> head;
        sl::Atomic<size_t> tail;
    };

    struct LogMessage
    {
        LogBuffer* textBuffer;
        size_t ticks;
        uint32_t begin;
        uint32_t length;
        size_t processorId;
        LogLevel loglevel;
        RunLevel runlevel;
        size_t threadId;
    };
    using LogMessageItem = sl::QueueMpSc<LogMessage>::Item;

    sl::QueueMpSc<LogMessage> msgQueue {};
    char earlyBufferStore[LogBufferSize];
    LogBuffer earlyBuffer =
    {
        .buffer = earlyBufferStore,
        .size = LogBufferSize,
        .head = 0,
        .tail = 0,
    };

    static LogMessageItem* AllocMessage(size_t length)
    {
        LogBuffer* buff = &earlyBuffer;
        if (CoreLocalAvailable() && CoreLocal()[LocalPtr::Logs] != nullptr)
            buff = static_cast<LogBuffer*>(CoreLocal()[LocalPtr::Logs]);

        const size_t realLength = sl::AlignUp(length + sizeof(LogMessageItem), sizeof(LogMessageItem));
        size_t begin = 0;
        size_t end = 0;

        for (size_t tries = 0; tries < MaxAllocExhangeTries; tries++)
        {
            begin = buff->head.Load();
            const size_t bigEnd = (static_cast<size_t>(begin) + realLength) % buff->size; //prevent truncation
            end = static_cast<size_t>(bigEnd);

            const size_t tail = buff->tail.Load();
            if (begin < tail && end >= tail)
                return nullptr; //new message would trample on tail of logbuffer
            if (buff->size - begin < realLength && end >= tail)
                return nullptr; //trampling, but with wraparound
            if (buff->head.CompareExchange(begin, end))
                break;
        }

        LogMessageItem* item = new(buff->buffer + begin) LogMessageItem();
        item->data.textBuffer = buff;
        item->data.length = length;
        item->data.begin = (begin + sizeof(LogMessageItem)) % buff->size;

        return item;
    }

    static void WriteoutLog(const LogMessage& msg)
    {
        if (msg.length == 0)
            return;

        size_t runover = 0;
        size_t length = msg.length;
        if (msg.begin + msg.length >= msg.textBuffer->size)
        {
            runover = msg.begin + msg.length - msg.textBuffer->size;
            length -= runover;
        }

        const size_t uptimeMajor = sl::ScaledTime(logTimescale, msg.ticks).ToMillis() / 1000;
        const size_t uptimeMinor = sl::ScaledTime(logTimescale, msg.ticks).ToMillis() % 1000;
        const size_t uptimeLen = npf_snprintf(nullptr, 0, UptimeStr.Begin(), uptimeMajor, uptimeMinor) + 1;
        char uptimeBuff[uptimeLen];
        npf_snprintf(uptimeBuff, uptimeLen, UptimeStr.Begin(), uptimeMajor, uptimeMinor);

        const size_t procId = msg.processorId == (size_t)-1 ? 0 : msg.processorId;
        const char* rlStr = RunLevelName(msg.runlevel);
        const size_t procThreadLen = npf_snprintf(nullptr, 0, ProcThreadStr.Begin(), procId, rlStr, msg.threadId) + 1;
        char procThreadBuff[procThreadLen];
        npf_snprintf(procThreadBuff, procThreadLen, ProcThreadStr.Begin(), procId, rlStr, msg.threadId);
        if (msg.processorId == (size_t)-1)
            procThreadBuff[0] = '?';


        for (size_t i = 0; i < logOutCount; i++)
        {
            if (logOuts[i]->Write == nullptr)
                continue;

            //print headers: uptime, processor + thread ids, level
            logOuts[i]->Write({ uptimeBuff, uptimeLen });
            logOuts[i]->Write({ procThreadBuff, procThreadLen });
            logOuts[i]->Write(LogLevelStrs[(unsigned)msg.loglevel]);

            //print message body + newline
            logOuts[i]->Write({ &msg.textBuffer->buffer[msg.begin], length });
            if (runover > 0)
                logOuts[i]->Write({ msg.textBuffer->buffer, runover });
            logOuts[i]->Write(EndLineStr);
        }

        msg.textBuffer->tail = (msg.begin + msg.length) % msg.textBuffer->size;
    }

    static void TryWriteLogs()
    {
        if (!logOutLock.TryLock())
            return;

        for (size_t printed = 0; printed < LogDrainLimit; printed++)
        {
            LogMessageItem* queueItem = msgQueue.Pop();
            if (queueItem == nullptr)
                break;
            WriteoutLog(queueItem->data);
        }
        logOutLock.Unlock();
    }

    void Log(const char* str, LogLevel level, ...)
    {
        va_list argsList;
        va_start(argsList, level);
        const size_t formattedLen = sl::Min<size_t>(npf_vsnprintf(nullptr, 0, str, argsList) + 1, MaxLogLength);
        va_end(argsList);
        
        char formatBuffer[formattedLen];
        va_start(argsList, level);
        npf_vsnprintf(formatBuffer, formattedLen, str, argsList);
        va_end(argsList);

        if (level == LogLevel::Fatal)
            PanicWithString({ formatBuffer, formattedLen });

        //between AllocMessage() and msgQueue.Push() we've taken space from a log buffer, but not
        //put the item into the queue (making the space reclaimable). Raising the runlevel (if that
        //subsystem is available) allows us to prevent being pre-empted by the scheduler and make this
        //part of the operation soft-atomic.
        sl::Opt<RunLevel> prevRl {};
        if (CoreLocalAvailable() && CurrentRunLevel() < RunLevel::Dpc)
            prevRl = RaiseRunLevel(RunLevel::Dpc);

        //Attempt to allocate from the global or core-local ringbuffers. We do this in a loop as 
        //allocation can (and does) fail, usually because there isnt enough space in the buffer.
        //If allocation fails we try flushing some messages from the buffer, and if that fails
        //we panic. Note we dont use ASSERT() here because calls Log() again, instead we call
        //Panic() directly.
        LogMessageItem* msg = nullptr;

        //TODO: tweak this code and switch to it over the infinite loop below.
        //I dont want infinite loops in the logging codepaths.
        //for (size_t i = 0; i < MaxAllocAttempts && msg == nullptr; i++)
        //{
        //    if (i != 0)
        //        TryWriteLogs();
        //    msg = AllocMessage(formattedLen);
        //}
        //if (msg == nullptr)
        //    Panic("Failed to allocate from kernel log buffer");
        while (msg == nullptr)
        {
            TryWriteLogs();
            msg = AllocMessage(formattedLen);
        }
        
        //we've got a place to store this message, field out the fields
        msg->data.ticks = GetUptime().ToScale(logTimescale).units;
        msg->data.loglevel = level;
        if (prevRl.HasValue())
            msg->data.runlevel = *prevRl;
        else if (CoreLocalAvailable())
            msg->data.runlevel = CoreLocal().runLevel;
        else
            msg->data.runlevel = RunLevel::Dpc;
        msg->data.processorId = CoreLocalAvailable() ? CoreLocal().id : -1;
        if (CoreLocalAvailable() && CoreLocal()[LocalPtr::Thread] != nullptr)
            //msg->data.threadId = Tasking::Thread::Current().Id(); //TODO: threadId
            msg->data.threadId = 0;

        if (msg->data.textBuffer->size < msg->data.begin + formattedLen) //handle wraparound
        {
            const size_t runover = (msg->data.begin + formattedLen) - msg->data.textBuffer->size;
            sl::memcopy(formatBuffer, 0, msg->data.textBuffer->buffer, msg->data.begin, formattedLen - runover);
            sl::memcopy(formatBuffer, formattedLen - runover, msg->data.textBuffer->buffer, 0, runover);
        }
        else
            sl::memcopy(formatBuffer, msg->data.textBuffer->buffer + msg->data.begin, formattedLen);

        msgQueue.Push(msg);
        TryWriteLogs();

        if (prevRl.HasValue())
            LowerRunLevel(*prevRl);
    }

    void InitGlobalLogging()
    {
        //TODO: tiny early framebuffer init with fixed sized buffer? replicated to all present laoder FBs
    }

    void InitLocalLogging(sl::Span<char> buffer)
    {
        auto localBuf = NewWired<LogBuffer>();
        localBuf->buffer = buffer.Begin();
        localBuf->size = buffer.Size();
        localBuf->head = 0;
        localBuf->tail = 0;

        CoreLocal()[LocalPtr::Logs] = localBuf;
    }

    void AddLogOutput(const LogOutput* output)
    {
        sl::ScopedLock scopeLock(logOutLock);

        VALIDATE_(logOutCount + 1 < MaxLogOutputs, );
        logOuts[logOutCount++] = output;
    }
}

namespace Npk
{
    extern sl::Atomic<size_t> panicFlag; //defined in Panic.cpp
}

namespace Npk::Core
{
    sl::Span<const LogOutput*> AcquirePanicOutputs(size_t tryLockCount)
    {
        ASSERT(panicFlag.Load() > 0, "Attempt to acquire panic outputs while not in a kernel panic");
        
        //other cores will eventually make their way to panic handlers, but they may already be
        //outputting logs or have interrupts disabled. So we make a local copy of the outputs,
        //and set the active output count to 0, so no one else uses them.
        //This prevents a handful of cases where the panic output gets corrupted.
        sl::Span<const LogOutput*> outs = { logOuts, logOutCount };
        logOutCount = 0;

        bool gotLock = false;
        for (size_t i = 0; i < tryLockCount; i++)
        {
            if ((gotLock = logOutLock.TryLock()) == true)
                break;
        }

        if (gotLock)
        {
            //since we're panicing, drain any logs in the queue if its safe to do so.
            sl::QueueMpSc<LogMessage>::Item* entry;
            logOutCount = outs.Size(); //we know no other cores are trying to use this now, its safe to restore the count
            while ((entry = msgQueue.Pop()) != nullptr)
                WriteoutLog(entry->data);
            logOutCount = 0;
        }
        else
        {
            for (size_t i = 0; i < outs.Size(); i++)
            {
                if (logOuts[i]->Write != nullptr)
                    logOuts[i]->Write(PanicLockFailedStr);
            }
        }
        return outs;
    }
}
