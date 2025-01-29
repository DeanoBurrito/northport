#include <core/Log.h>
#include <core/Clock.h>
#include <core/WiredHeap.h>
#include <core/Config.h>
#include <interfaces/loader/Generic.h>
#include <Panic.h>
#include <Hhdm.h>
#include <Locks.h>
#include <Maths.h>
#include <NanoPrintf.h>
#include <Terminal.h>
#include <Memory.h>
#include <PlacementNew.h>

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
    constexpr size_t MaxUptimeLen = 16;
    constexpr size_t MaxProcThreadLen = 16;

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
        if (CoreLocalAvailable() && GetLocalPtr(SubsysPtr::Logs) != nullptr)
            buff = static_cast<LogBuffer*>(GetLocalPtr(SubsysPtr::Logs));

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

    static void WriteoutLog(const LogMessage& msg, sl::Span<const LogOutput*> outputs)
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

        const size_t timestampMillis = sl::TimeCount(logTimescale, msg.ticks).Rebase(sl::Millis).ticks;
        const size_t uptimeMajor = timestampMillis / 1000;
        const size_t uptimeMinor = timestampMillis % 1000;
        const size_t uptimeLen = sl::Min<size_t>(npf_snprintf(nullptr, 0, UptimeStr.Begin(), uptimeMajor, uptimeMinor) + 1, MaxUptimeLen);
        char uptimeBuff[MaxUptimeLen];
        npf_snprintf(uptimeBuff, uptimeLen, UptimeStr.Begin(), uptimeMajor, uptimeMinor);

        const size_t procId = msg.processorId == (size_t)-1 ? 0 : msg.processorId;
        const char* rlStr = RunLevelName(msg.runlevel);
        const size_t procThreadLen = sl::Min<size_t>(npf_snprintf(nullptr, 0, ProcThreadStr.Begin(), procId, rlStr, msg.threadId) + 1, MaxProcThreadLen);
        char procThreadBuff[MaxProcThreadLen];
        npf_snprintf(procThreadBuff, procThreadLen, ProcThreadStr.Begin(), procId, rlStr, msg.threadId);
        if (msg.processorId == (size_t)-1)
            procThreadBuff[0] = '?';

        for (size_t i = 0; i < outputs.Size(); i++)
        {
            if (outputs[i]->Write == nullptr)
                continue;

            //print headers: uptime, processor + thread ids, level
            outputs[i]->Write({ uptimeBuff, uptimeLen });
            outputs[i]->Write({ procThreadBuff, procThreadLen });
            outputs[i]->Write(LogLevelStrs[(unsigned)msg.loglevel]);

            //print message body + newline
            outputs[i]->Write({ &msg.textBuffer->buffer[msg.begin], length });
            if (runover > 0)
                outputs[i]->Write({ msg.textBuffer->buffer, runover });
            outputs[i]->Write(EndLineStr);
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
            WriteoutLog(queueItem->data, sl::Span(logOuts, logOutCount));
        }
        logOutLock.Unlock();
    }

    void Log(const char* str, LogLevel level, ...)
    {
        va_list argsList;
        va_start(argsList, level);
        const size_t formattedLen = sl::Min<size_t>(npf_vsnprintf(nullptr, 0, str, argsList) + 1, MaxLogLength);
        va_end(argsList);
        
        char formatBuffer[MaxLogLength];
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
        msg->data.ticks = GetUptime().Rebase(logTimescale).ticks;
        msg->data.loglevel = level;
        if (prevRl.HasValue())
            msg->data.runlevel = *prevRl;
        else if (CoreLocalAvailable())
            msg->data.runlevel = CurrentRunLevel();
        else
            msg->data.runlevel = RunLevel::Dpc;
        msg->data.processorId = CoreLocalAvailable() ? CoreLocalId() : -1;
        if (CoreLocalAvailable() && GetLocalPtr(SubsysPtr::Thread) != nullptr)
            //msg->data.threadId = Tasking::Thread::Current().Id(); //TODO: threadId
            msg->data.threadId = 0;

        if (msg->data.textBuffer->size < msg->data.begin + formattedLen) //handle wraparound
        {
            const size_t runover = (msg->data.begin + formattedLen) - msg->data.textBuffer->size;
            sl::MemCopy(&msg->data.textBuffer->buffer[msg->data.begin], &formatBuffer[0], formattedLen - runover);
            sl::MemCopy(&msg->data.textBuffer->buffer[0], &formatBuffer[formattedLen - runover], runover);
        }
        else
            sl::MemCopy(&msg->data.textBuffer->buffer[msg->data.begin], formatBuffer, formattedLen);

        msgQueue.Push(msg);
        TryWriteLogs();

        if (prevRl.HasValue())
            LowerRunLevel(*prevRl);
    }

    struct FramebufferLogger
    {
        sl::FwdListHook next;
        sl::Terminal renderer;
    };

    sl::FwdList<FramebufferLogger, &FramebufferLogger::next> fbTerms;

    static void* FramebufferLoggerAlloc(size_t count)
    {
        auto maybeAlloc = EarlyPmAlloc(count);
        if (maybeAlloc.HasValue())
            return reinterpret_cast<void*>(*maybeAlloc + hhdmBase);
        return nullptr;
    }

    static void FramebufferLoggerWrite(sl::StringSpan message)
    {
        for (auto it = fbTerms.Begin(); it != fbTerms.End(); ++it)
            it->renderer.Write(message, true);
    }

    static void FramebufferLoggerBeginPanic()
    {}

    LogOutput fbLoggerOutput
    {
        .Write = FramebufferLoggerWrite,
        .BeginPanic = FramebufferLoggerBeginPanic,
    };

    void InitGlobalLogging()
    {
        if (GetConfigNumber("kernel.log.no_fb_output", false))
            return;

        /* The only built-in log sink is the terminal renderer, if
         * we get any framebuffers from the bootloader we create a
         * renderer instance for each of them so we can get log output
         * asap - especially useful on real hardware.
         * It is a little wasteful because we (currently) cant reclaim
         * early physical allocations.
         */
        sl::TerminalConfig config 
        {
            .colours = { 0x00000000, 0x00AA0000, 0x0000AA00, 0x00AA5500, 0x000000AA, 0x00AA00AA, 0x0000AAAA, 0x00AAAAAA },
            .brightColours = { 0x00555555, 0x00FF5555, 0x0055FF55, 0x00FFFF55, 0x005555FF, 0x00FF55FF, 0x0055FFFF, 0x00FFFFFF },
            .background = 0x0,
            .foreground = 0xFFFFFF,
            .tabSize = 4,
            .margin = 0,
            .fontSpacing = 0,
            .fbBase = {},
            .fbSize = {},
            .fbStride = {},
            .Alloc = FramebufferLoggerAlloc,
        };

        LoaderFramebuffer fbStore[MaxLogOutputs];
        const size_t fbCount = GetFramebuffers(fbStore, 0);
        for (size_t i = 0; i < fbCount; i++)
        {
            auto maybeFbTerm = EarlyPmAlloc(sizeof(FramebufferLogger));
            if (!maybeFbTerm.HasValue())
                return; //failed to allocate, stop here

            config.fbBase = reinterpret_cast<void*>(fbStore[i].address);
            config.fbStride = fbStore[i].stride;
            config.fbSize = { fbStore[i].width, fbStore[i].height };
            auto term = new(reinterpret_cast<void*>(*maybeFbTerm + hhdmBase)) FramebufferLogger();
            if (term->renderer.Init(config))
                fbTerms.PushBack(term);
        }

        if (!fbTerms.Empty())
            AddLogOutput(&fbLoggerOutput);
    }

    void InitLocalLogging(sl::Span<char> buffer)
    {
        auto localBuf = NewWired<LogBuffer>();
        VALIDATE_(localBuf != nullptr, );

        localBuf->buffer = buffer.Begin();
        localBuf->size = buffer.Size();
        localBuf->head = 0;
        localBuf->tail = 0;

        SetLocalPtr(SubsysPtr::Logs, localBuf);
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

        bool gotLock = false;
        for (size_t i = 0; i < tryLockCount; i++)
        {
            if ((gotLock = logOutLock.TryLock()) == true)
                break;
        }
        logOutCount = 0;

        if (gotLock)
        {
            //since we're panicing, drain any logs in the queue if its safe to do so.
            sl::QueueMpSc<LogMessage>::Item* entry;
            while ((entry = msgQueue.Pop()) != nullptr)
                WriteoutLog(entry->data, outs);
        }
        else
        {
            for (size_t i = 0; i < outs.Size(); i++)
            {
                if (outs[i]->Write != nullptr)
                    outs[i]->Write(PanicLockFailedStr);
            }
        }
        return outs;
    }
}
