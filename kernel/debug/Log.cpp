#include <debug/Log.h>
#include <debug/Symbols.h>
#include <debug/Panic.h>
#include <memory/Pmm.h>
#include <tasking/Clock.h>
#include <tasking/Threads.h>
#include <tasking/RunLevels.h>
#include <containers/Queue.h>
#include <NanoPrintf.h>
#include <Locks.h>
#include <Memory.h>
#include <Maths.h>

namespace Npk::Debug
{
    constexpr size_t MaxLogOutputs = 4;
    constexpr size_t MaxLogDrainCount = 32;
    constexpr size_t CoreLogBufferSize = 4 * PageSize;
    constexpr size_t EarlyBufferSize = 4 * PageSize;
    constexpr size_t MaxMessageLength = 128;

    constexpr const char* LevelStrs[] = 
    {
        "[ Fatal ] ", "[ Error ] ", "[Warning] ",
        "[ Info  ] ", "[Verbose] ", "[ Debug ] "
    };

    constexpr const char* LevelColourStrs[] =
    {
        "\e[91m", "\e[31m", "\e[93m",
        "\e[97m", "\e[90m", "\e[94m",
    };

    constexpr const char* LevelColourReset = "\e[39m";
    constexpr size_t LevelStrLength = 10;
    constexpr size_t LevelColourLength = 5;

    struct LogBuffer
    {
        char* buffer;
        size_t length;
        size_t head;
    };

    struct LogMessage
    {
        LogBuffer* buff;
        size_t begin;
        size_t length;
    };

    sl::QueueMpSc<LogMessage> msgQueue {};
    char earlyBufferStore[EarlyBufferSize];
    LogBuffer earlyBuffer = 
    { 
        .buffer = earlyBufferStore, 
        .length = EarlyBufferSize, 
        .head = 0
    };

    LogOutput* logOuts[MaxLogOutputs];
    size_t logOutCount = 0;
    sl::SpinLock logOutLock;

    void WriteToOutputs(const LogMessage& msg)
    {
        if (msg.length == 0)
            return;

        size_t runover = 0;
        size_t length = msg.length;
        if (msg.begin + msg.length >= msg.buff->length)
        {
            runover = msg.begin + msg.length - msg.buff->length;
            length -= runover;
        }

        for (size_t i = 0; i < logOutCount; i++)
        {
            if (logOuts[i]->Write == nullptr)
                continue;

            logOuts[i]->Write({ &msg.buff->buffer[msg.begin], length });
            if (runover > 0)
                logOuts[i]->Write({ msg.buff->buffer, runover });
        }
    }

    void WriteLog(const char* message, size_t messageLen, LogBuffer* buffer)
    {
        using QueueItem = sl::QueueMpSc<LogMessage>::Item;

        //TODO: disable scheduler pre-emption here while we have reserved buffer space, but havent added it to the message queue.
        //we allocate space for message data and the queue item in the same go
        const size_t allocLength = messageLen + 2 * sizeof(QueueItem);
        const size_t allocBegin = __atomic_fetch_add(&buffer->head, allocLength, __ATOMIC_SEQ_CST) % buffer->length;
        
        uintptr_t itemAddr = 0;
        size_t messageBegin = allocBegin;
        if (allocBegin + allocLength > buffer->length)
        {
            //determine whether to place the queue entry before or after the text, depending on
            //where there is space.
            size_t cutOffset = messageLen - ((messageBegin + messageLen) - buffer->length);
            if (sl::AlignUp(allocBegin, alignof(QueueItem)) + sizeof(QueueItem) < buffer->length)
            {
                itemAddr = sl::AlignUp(allocBegin, alignof(QueueItem));
                messageBegin = itemAddr + sizeof(QueueItem);
                cutOffset = sl::Min(messageLen - ((messageBegin + messageLen) - buffer->length), messageLen);

                sl::memcopy(message, 0, buffer->buffer, messageBegin, cutOffset);
                sl::memcopy(message, cutOffset, buffer->buffer, 0, messageLen - cutOffset);
            }
            else if (sl::AlignUp(cutOffset, alignof(QueueItem)) + sizeof(QueueItem) < allocLength - cutOffset)
            {
                itemAddr = sl::AlignUp(cutOffset, alignof(QueueItem));
                sl::memcopy(message, 0, buffer->buffer, allocBegin, cutOffset);
                sl::memcopy(message, cutOffset, buffer->buffer, 0, messageLen - cutOffset);
            }
            else
                Panic("WriteLog() failed to alloc message queue struct");
        }
        else
        {
            //no wraparound, this is very easy
            itemAddr = sl::AlignUp(allocBegin, alignof(QueueItem));
            messageBegin += 2 * sizeof(QueueItem);
            sl::memcopy(message, 0, buffer->buffer, messageBegin, messageLen);
        }

        QueueItem* item = new(&buffer->buffer[itemAddr]) QueueItem();
        item->data.begin = messageBegin;
        item->data.buff = buffer;
        item->data.length = messageLen;

        //at this point we just need to add this log message to the queue
        msgQueue.Push(item);

        //if can take the output lock, drain the message queue now, otherwise leave it for whoever is holding the lock
        if (logOutLock.TryLock())
        {
            for (size_t printed = 0; printed < MaxLogDrainCount; printed++)
            {
                QueueItem* item = msgQueue.Pop();
                if (item == nullptr)
                    break;
                WriteToOutputs(item->data);
            }

            logOutLock.Unlock();
        }
    }
    
    void Log(const char* str, LogLevel level, ...)
    {
        //get length of uptime
        const size_t uptime = Tasking::GetUptime().ToMillis();
        const size_t uptimeLen = npf_snprintf(nullptr, 0, "%lu.%03lu", uptime / 1000, uptime % 1000) + 1;

        //get length of header (processor id + thread id)
        const size_t processorId = CoreLocalAvailable() ? CoreLocal().id : 0;
        size_t threadId = 0;
        if (CoreLocalAvailable() && CoreLocal()[LocalPtr::Thread] != nullptr)
            threadId = static_cast<Tasking::Thread*>(CoreLocal()[LocalPtr::Thread])->Id();
        const size_t headerLen = npf_snprintf(nullptr, 0, "p%lut%lu", processorId, threadId) + 1;

        //get formatted message length
        va_list argsList;
        va_start(argsList, level);
        const size_t strLen = sl::Min<size_t>(npf_vsnprintf(nullptr, 0, str, argsList) + 1, MaxMessageLength);
        va_end(argsList);

        //create buffer on stack, +2 to allow space for "\r\n".
        const size_t bufferLen = uptimeLen + LevelStrLength + headerLen + 
            (LevelColourLength * 2) + strLen + 2;
        char buffer[bufferLen];
        size_t bufferStart = 0;

        //print uptime
        npf_snprintf(buffer, uptimeLen, "%lu.%03lu", uptime / 1000, uptime % 1000);
        bufferStart += uptimeLen;
        buffer[bufferStart - 1] = ' ';

        //print processor + thread ids
        npf_snprintf(&buffer[bufferStart], headerLen, "p%lut%lu", processorId, threadId);
        bufferStart += headerLen;
        buffer[bufferStart - 1] = ' ';

        //add level string (+ colouring escape codes)
        sl::memcopy(LevelColourStrs[(size_t)level], 0, buffer, bufferStart, LevelColourLength);
        bufferStart += LevelColourLength;
        sl::memcopy(LevelStrs[(size_t)level], 0, buffer, bufferStart, LevelStrLength);
        bufferStart += LevelStrLength;
        sl::memcopy(LevelColourReset, 0, buffer, bufferStart, LevelColourLength);
        bufferStart += LevelColourLength;

        //format and print log message
        const size_t textStart = bufferStart;
        va_start(argsList, level);
        npf_vsnprintf(&buffer[bufferStart], strLen, str, argsList);
        va_end(argsList);
        bufferStart += strLen;
        
        buffer[bufferStart++] = '\r';
        buffer[bufferStart++] = '\n';

        if (level == LogLevel::Fatal)
            Panic({ &buffer[textStart], bufferLen });

        if (CoreLocalAvailable() && CoreLocal()[LocalPtr::Log] != nullptr)
            WriteLog(buffer, bufferLen, reinterpret_cast<LogBuffer*>(CoreLocal()[LocalPtr::Log]));
        else
            WriteLog(buffer, bufferLen, &earlyBuffer);
    }

    void InitCoreLogBuffers()
    {
        LogBuffer* buffer = new LogBuffer;
        buffer->head = 0;
        buffer->length = CoreLogBufferSize;
        buffer->buffer = reinterpret_cast<char*>(PMM::Global().Alloc(CoreLogBufferSize / PageSize) + hhdmBase);

        CoreLocal()[LocalPtr::Log] = buffer;
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

        for (size_t i = 0; i < tryLockCount; i++)
        {
            if (logOutLock.TryLock())
                break;
        }

        return { logOuts, logOutCount };
    }
}
