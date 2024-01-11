#include <debug/Log.h>
#include <arch/Platform.h>
#include <debug/Symbols.h>
#include <drivers/DriverManager.h>
#include <interrupts/Ipi.h>
#include <memory/Pmm.h>
#include <memory/Vmm.h>
#include <tasking/Clock.h>
#include <tasking/Thread.h>
#include <containers/Queue.h>
#include <NanoPrintf.h>
#include <Locks.h>
#include <Memory.h>
#include <Maths.h>

namespace Npk::Debug
{
    constexpr size_t PanicLockAttempts = 4321'0000;
    constexpr size_t MaxEarlyLogOuts = 4;
    constexpr size_t CoreLogBufferSize = 4 * PageSize;
    constexpr size_t EarlyBufferSize = 4 * PageSize;
    constexpr size_t MaxEloItemsPrinted = 20;
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

    EarlyLogWrite earlyOuts[MaxEarlyLogOuts];
    size_t eloCount = 0;
    sl::SpinLock eloLock;

    void WriteElos(const LogMessage& msg)
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

        for (size_t i = 0; i < MaxEarlyLogOuts; i++)
        {
            if (earlyOuts[i] == nullptr)
                continue;

            earlyOuts[i](&msg.buff->buffer[msg.begin], length);
            if (runover > 0)
                earlyOuts[i](msg.buff->buffer, runover);
        }
    }

    void WriteLog(const char* message, size_t messageLen, LogBuffer* buffer)
    {
        using QueueItem = sl::QueueMpSc<LogMessage>::Item;

        //TODO: disable scheduler pre-emption here while we have reserved buffer space, but havent added it to the message queue.
        //we allocate space for message data and the queue item in the same go
        const size_t allocLength = messageLen + 2 * sizeof(QueueItem);
        const size_t beginWrite = __atomic_fetch_add(&buffer->head, allocLength, __ATOMIC_SEQ_CST) % buffer->length;
        
        uintptr_t itemAddr = 0;
        size_t messageBegin = beginWrite;
        if (beginWrite + allocLength > buffer->length)
        {
            //we're going to wrap around, this is fine for message text but not for the queue struct,
            //as it needs to be contiguous in memory.
            size_t endLength = (beginWrite + allocLength) - buffer->length;
            size_t startLength = allocLength - endLength;

            //place struct before message if there's space
            if (startLength >= 2 * sizeof(QueueItem)) //TODO: check **where** the free space is available, otherwise make it.
            {
                itemAddr = beginWrite;
                startLength -= 2 * sizeof(QueueItem);
                messageBegin += 2 * sizeof(QueueItem);
            }
            else //otherwise place it after the message
            {
                endLength -= 2 * sizeof(QueueItem);
                itemAddr = endLength;
            }
            
            sl::memcopy(message, 0, buffer->buffer, messageBegin, startLength);
            sl::memcopy(message, startLength, buffer->buffer, 0, endLength);
        }
        else
        {
            //no wraparound, this is very easy
            itemAddr = beginWrite;
            messageBegin += 2 * sizeof(QueueItem);
            sl::memcopy(message, 0, buffer->buffer, messageBegin, messageLen);
        }

        QueueItem* item = new(sl::AlignUp(&buffer->buffer[itemAddr], sizeof(QueueItem))) QueueItem();
        item->data.begin = messageBegin;
        item->data.buff = buffer;
        item->data.length = messageLen;

        //at this point we just need to add this log message to the queue
        msgQueue.Push(item);

        //if we're in the early state (eloCount > 0), try to flush the message queue to the outputs
        //TODO: we could be more efficient by locking the scheduler to this thread while writing.
        if (__atomic_load_n(&eloCount, __ATOMIC_RELAXED) > 0 && eloLock.TryLock())
        {
            for (size_t printed = 0; printed < MaxEloItemsPrinted; printed++)
            {
                QueueItem* item = msgQueue.Pop();
                if (item == nullptr)
                    break;
                
                WriteElos(item->data);
            }

            eloLock.Unlock();
        }
    }
    
    void Log(const char* str, LogLevel level, ...)
    {
        //get length of uptime
        const size_t uptime = Tasking::GetUptime();
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
            Panic(&buffer[textStart]);

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

    void AddEarlyLogOutput(EarlyLogWrite callback)
    {
        sl::ScopedLock scopeLock(eloLock);
        if (__atomic_load_n(&eloCount, __ATOMIC_RELAXED) == 0) //deferred init
        {
            for (size_t i = 0; i < MaxEarlyLogOuts; i++)
                earlyOuts[i] = nullptr;
        }

        for (size_t i = 0; i < MaxEarlyLogOuts; i++)
        {
            if (earlyOuts[i] != nullptr)
                continue;

            __atomic_add_fetch(&eloCount, 1,__ATOMIC_RELAXED);
            earlyOuts[i] = callback;
            return;
        }
    }

    void AttachLogDriver(size_t deviceId)
    {
        (void)deviceId;
        ASSERT_UNREACHABLE();
    }

    void DetachLogDriver(size_t deviceId)
    {
        (void)deviceId;
        ASSERT_UNREACHABLE();
    }

    void LogWriterServiceMain(void*)
    {
        //TODO: if ELOs are active, flush those, otherwise update each driver with the new
        //read head.
    }

    void PanicWrite(const char* message, ...)
    {
        using namespace Npk::Debug;

        va_list args;
        va_start(args, message);
        const size_t messageLen = npf_vsnprintf(nullptr, 0, message, args) + 1;
        va_end(args);

        char messageBuffer[messageLen];
        va_start(args, message);
        npf_vsnprintf(messageBuffer, messageLen, message, args);
        va_end(args);

        for (size_t i = 0; i < MaxEarlyLogOuts; i++)
        {
            if (earlyOuts[i] != nullptr)
                earlyOuts[i](messageBuffer, messageLen);
        }
    }
}

extern "C"
{
    static_assert(Npk::Debug::EarlyBufferSize > 0x2000, "Early buffer is too small to used as a panic stack");
    void* panicStack = &Npk::Debug::earlyBufferStore[Npk::Debug::EarlyBufferSize];

    void PanicLanding(const char* reason)
    {
        using namespace Npk;
        using namespace Npk::Debug;

        Interrupts::BroadcastPanicIpi();
        //try to take the ELO lock a reasonable number of times. Don't wait on the lock though
        //as it's possible to deadlock here. This also gives other cores time to finish accept 
        //and handle the panic IPI, which helps prevent corrupting any attached outputs.
        for (size_t i = 0; i < PanicLockAttempts; i++)
            eloLock.TryLock();

        //drain the message queue, the last messages printed may contain critical info.
        for (auto* msg = msgQueue.Pop(); msg != nullptr; msg = msgQueue.Pop())
            WriteElos(msg->data); //TODO: dont rely on external logic like this during panic

        PanicWrite("       )\r\n");
        PanicWrite("    ( /(                        (\r\n");
        PanicWrite("    )\\())  (   (             (  )\\             )        (\r\n");
        PanicWrite("   ((_)\\  ))\\  )(    (      ))\\((_)  `  )   ( /(   (    )\\   (\r\n");
        PanicWrite("  (_ ((_)/((_)(()\\   )\\ )  /((_)_    /(/(   )(_))  )\\ )((_)  )\\\r\n");
        PanicWrite("  | |/ /(_))   ((_) _(_/( (_)) | |  ((_)_\\ ((_)_  _(_/( (_) ((_)\r\n");
        PanicWrite("  | ' < / -_) | '_|| ' \\))/ -_)| |  | '_ \\)/ _` || ' \\))| |/ _|\r\n");
        PanicWrite("  |_|\\_\\\\___| |_|  |_||_| \\___||_|  | .__/ \\__,_||_||_| |_|\\__|\r\n");
        PanicWrite("                                    |_|\r\n");
        PanicWrite("\r\n");
        PanicWrite(reason);
        PanicWrite("\r\n");

        //print core-local info (this is always available)
        constexpr const char* RunLevelStrs[] = { "Normal", "Dispatch", "IntHandler" };
        static_assert((size_t)RunLevel::Normal == 0, "RunLevel integer representation updated.");
        static_assert((size_t)RunLevel::IntHandler == 2, "RunLevel integer representation updated.");
        
        if (CoreLocalAvailable())
        {
            CoreLocalInfo& clb = CoreLocal();
            PanicWrite("Processor %lu: runLevel=%s, logBuffer=0x%lx\r\n", 
                clb.id, RunLevelStrs[(size_t)clb.runLevel], (uintptr_t)clb[LocalPtr::Log]);
        }
        else
            PanicWrite("Core-local information not available.\r\n");

        //print thread-local info
        if (CoreLocalAvailable() && CoreLocal()[LocalPtr::Thread] != nullptr)
        {
            auto& thread = Tasking::Thread::Current();
            auto& process = thread.Parent();

            const char* shadowName = "<none>";
            auto shadow = Drivers::DriverManager::Global().GetShadow();
            if (shadow.Valid())
                shadowName = shadow->friendlyName.C_Str();

            PanicWrite("Thread: id=%lu, driverShadow=%s, procId=%lu, procName=%s",
                thread.Id(), shadowName, process.Id(), "n/a");
        }
        else
            PanicWrite("Thread information not available.\r\n");

        //print the stack trace
        PanicWrite("\r\nCall stack (latest call first):\r\n");
        for (size_t i = 0; i < 20; i++)
        {
            const uintptr_t addr = GetReturnAddr(i);
            if (addr == 0)
                break;
            auto symbol = SymbolFromAddr(addr, SymbolFlag::Public | SymbolFlag::Private);
            const char* symbolName = symbol.HasValue() ? symbol->name.Begin() : "<unknown>";
            const size_t symbolOffset = symbol.HasValue() ? addr - symbol->base : 0;
            PanicWrite("%3u: 0x%lx %s+0x%lu\r\n", i, addr, symbolName, symbolOffset);
        }
        
        PanicWrite("\r\nSystem has halted indefinitely, manual reset required.\r\n");
        Halt();
    }
}
