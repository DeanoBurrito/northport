#include <debug/Log.h>
#include <arch/Platform.h>
#include <debug/Panic.h>
#include <memory/Pmm.h>
#include <tasking/Clock.h>
#include <tasking/Threads.h>
#include <Maths.h>
#include <Locks.h>
#include <NanoPrintf.h>

namespace Npk::Debug
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

    LogOutput* logOuts[MaxLogOutputs];
    size_t logOutCount = 0;
    sl::SpinLock logOutLock;

    constexpr sl::TimeScale logTimescale = sl::TimeScale::Millis;
    constexpr size_t LogDrainLimit = 64;
    constexpr size_t MaxAllocExhangeTries = 4;
    constexpr size_t MaxAllocAttempts = 4;
    constexpr size_t LogBufferSize = 0x4000;

#ifdef __riscv
    /* This is done because older (but not that old) versions of gcc/clang require
     * support from libatomic to implement 16-bit atomic ops, which I am providing.
     */
    using LogBuffIdx = uint32_t;
#else
    using LogBuffIdx = uint16_t;
#endif
    static_assert(__atomic_always_lock_free(sizeof(LogBuffIdx), 0));
    static_assert((LogBuffIdx)-1 >= LogBufferSize);

    struct LogBuffer
    {
        char* buffer;
        LogBuffIdx size;
        sl::Atomic<LogBuffIdx> head;
        sl::Atomic<LogBuffIdx> tail;
    };

    struct LogMessage
    {
        LogBuffer* textBuffer;
        size_t ticks;
        LogBuffIdx begin;
        LogBuffIdx length;
        LogBuffIdx processorId;
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
        LogBuffIdx begin = 0;
        LogBuffIdx end = 0;

        for (size_t tries = 0; tries < MaxAllocExhangeTries; tries++)
        {
            begin = buff->head.Load();
            const size_t bigEnd = (static_cast<size_t>(begin) + realLength) % buff->size; //prevent truncation
            end = static_cast<LogBuffIdx>(bigEnd);

            const LogBuffIdx tail = buff->tail.Load();
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

        const size_t procId = msg.processorId == (LogBuffIdx)-1 ? 0 : msg.processorId;
        const char* rlStr = Tasking::GetRunLevelName(msg.runlevel);
        const size_t procThreadLen = npf_snprintf(nullptr, 0, ProcThreadStr.Begin(), procId, rlStr, msg.threadId) + 1;
        char procThreadBuff[procThreadLen];
        npf_snprintf(procThreadBuff, procThreadLen, ProcThreadStr.Begin(), procId, rlStr, msg.threadId);
        if (msg.processorId == (LogBuffIdx)-1)
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
            Panic({ formatBuffer, formattedLen });

        //between AllocMessage() and msgQueue.Push() we've taken space from a log buffer, but not
        //put the item into the queue (making the space reclaimable). Raising the runlevel (if that
        //subsystem is available) allows us to prevent being pre-empted by the scheduler and make this
        //part of the operation soft-atomic.
        sl::Opt<RunLevel> prevRl {};
        if (CoreLocalAvailable())
            prevRl = Tasking::EnsureRunLevel(RunLevel::Dpc);

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
        msg->data.ticks = Tasking::GetUptime().ToScale(logTimescale).units;
        msg->data.loglevel = level;
        if (prevRl.HasValue())
            msg->data.runlevel = *prevRl;
        else if (CoreLocalAvailable())
            msg->data.runlevel = CoreLocal().runLevel;
        else
            msg->data.runlevel = RunLevel::Dpc;
        msg->data.processorId = CoreLocalAvailable() ? CoreLocal().id : -1;
        if (CoreLocalAvailable() && CoreLocal()[LocalPtr::Thread] != nullptr)
            msg->data.threadId = Tasking::Thread::Current().Id();

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
            Tasking::LowerRunLevel(*prevRl);
    }

    void InitCoreLogBuffers()
    {
        LogBuffer* buffer = new LogBuffer;
        buffer->head = buffer->tail = 0;
        buffer->size = LogBufferSize;
        buffer->buffer = AddHhdm(reinterpret_cast<char*>(PMM::Global().Alloc(LogBufferSize / PageSize)));

        CoreLocal()[LocalPtr::Logs] = buffer;
    }

    void AddLogOutput(LogOutput* output)
    {
        sl::ScopedLock scopeLock(logOutLock);

        VALIDATE_(logOutCount + 1 < MaxLogOutputs, );
        logOuts[logOutCount++] = output;
    }

    extern sl::Atomic<size_t> panicFlag; //defined in debug/Panic.cpp
    sl::Span<LogOutput*> AcquirePanicOutputs(size_t tryLockCount)
    {
        ASSERT(panicFlag.Load() > 0, "Attempt to acquire panic outputs while not in a kernel panic");
        
        //other cores will eventually make their way to panic handlers, but they may already be
        //outputting logs or have interrupts disabled. So we make a local copy of the outputs,
        //and set the active output count to 0, so no one else uses them.
        //This prevents a handful of cases where the panic output gets corrupted.
        sl::Span<LogOutput*> outs = { logOuts, logOutCount };
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

    constexpr size_t ShortTraceDepth = 8;
    const size_t ShortTraceBufferCount = 128;

    struct ShortTrace
    {
        uintptr_t callstack[ShortTraceDepth];
    };

    ShortTrace traceBuffer[ShortTraceBufferCount];
    sl::Atomic<size_t> nextTraceId {};

    size_t GenerateShortTrace(sl::Opt<uintptr_t> begin)
    {
        uintptr_t callstack[ShortTraceDepth] {};

        for (size_t i = 0; i < ShortTraceDepth; i++)
            callstack[i] = GetReturnAddr(i, begin.HasValue() ? *begin : 0);

        return StoreShortTrace(callstack);
    }

    size_t StoreShortTrace(sl::Span<uintptr_t> callstack)
    {
        const size_t id = nextTraceId++;
        const size_t index = id % ShortTraceBufferCount;

        for (size_t i = 0; i < callstack.Size() && i < ShortTraceDepth; i++)
            traceBuffer[index].callstack[i] = callstack[i];

        return id;
    }

    bool GetShortTrace(size_t id, sl::Span<uintptr_t> callstack)
    {
        if (id < sl::AlignDown(nextTraceId.Load(), ShortTraceBufferCount))
            return false; //id is too old, it's been overwritten

        const size_t index = id % ShortTraceBufferCount;
        for (size_t i = 0; i < callstack.Size() && i < ShortTraceDepth; i++)
            callstack[i] = traceBuffer[index].callstack[i];

        return true;
    }
}
