#include <debug/Log.h>
#include <arch/Platform.h>
#include <debug/NanoPrintf.h>
#include <memory/Pmm.h>
#include <tasking/Clock.h>
#include <tasking/Thread.h>
#include <Locks.h>
#include <Memory.h>

namespace Npk::Debug
{
    constexpr size_t MaxEarlyLogOuts = 4;
    constexpr size_t CoreLogBufferSize = 4 * PageSize;
    constexpr size_t EarlyBufferSize = 0x4000;

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

    struct CoreLogBuffer
    {
        char* buffer;
        size_t head;
        size_t reserveHead;
        size_t eloLastHead;
        size_t lock;
    };

    char earlyBufferStore[EarlyBufferSize];
    CoreLogBuffer earlyBuffer;

    EarlyLogWrite earlyOuts[MaxEarlyLogOuts];
    size_t eloCount = 0;
    sl::TicketLock eloLock;

    void WriteToEarlyOuts(const char* buffer, size_t length)
    {
        if (__atomic_load_n(&eloCount, __ATOMIC_RELAXED) != 0)
        {
            sl::ScopedLock scopeLock(eloLock);
            for (size_t i = 0; i < MaxEarlyLogOuts; i++)
            {
                if (earlyOuts[i] != nullptr)
                    earlyOuts[i](buffer, length);
            }
        }
    }

    void WriteBufferToEarlyOuts(const char* buffer, size_t base, size_t length)
    {
        if (length == 0)
            return;

        base = base % (CoreLogBufferSize);
        size_t runover = 0;
        if (base + length >= CoreLogBufferSize)
        {
            runover = base + length - (CoreLogBufferSize);
            length  -= runover;
        }

        WriteToEarlyOuts(&buffer[base], length);
        if (runover > 0)
            WriteToEarlyOuts(buffer, runover);
    }

    void WriteLog(const char* msg, size_t msgLength, size_t bufferLength, CoreLogBuffer& buffer)
    {
        __atomic_add_fetch(&buffer.lock, 1, __ATOMIC_ACQUIRE); //acquire lock
        const size_t beginWrite = __atomic_fetch_add(&buffer.reserveHead, msgLength, __ATOMIC_RELAXED);

        size_t overrunLength = 0;
        if (beginWrite + msgLength >=- bufferLength)
        {
            overrunLength = (beginWrite + msgLength) - bufferLength;
            msgLength -= overrunLength;
        }

        sl::memcopy(msg, 0, buffer.buffer, beginWrite, msgLength);
        if (overrunLength > 0)
            sl::memcopy(msg, msgLength, buffer.buffer, 0, overrunLength);
        
        const size_t releaseCount = __atomic_sub_fetch(&buffer.lock, 1, __ATOMIC_RELEASE); //release lock

        //if we were the last to unlock, update the public head with the reserved space head
        if (releaseCount == 0)
        {
            const size_t newHead = __atomic_load_n(&buffer.reserveHead, __ATOMIC_RELAXED);
            __atomic_store_n(&buffer.head, newHead, __ATOMIC_RELAXED);

            if (__atomic_load_n(&eloCount, __ATOMIC_RELAXED) != 0)
            {
                //we're in early init, write to the early log outputs
                const size_t beginRead = __atomic_exchange_n(&buffer.eloLastHead, newHead, __ATOMIC_RELAXED);
                WriteBufferToEarlyOuts(buffer.buffer, beginRead, newHead - beginRead);
            }
            //else: ELOs are not present, meaning service thread is active and will flush buffers for us.
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
        if (CoreLocalAvailable() && CoreLocal().schedThread != nullptr)
            threadId = static_cast<Tasking::Thread*>(CoreLocal().schedThread)->Id();
        const size_t headerLen = npf_snprintf(nullptr, 0, "p%lut%lu", processorId, threadId) + 1;

        //get formatted message length
        va_list argsList;
        va_start(argsList, level);
        const size_t strLen = npf_vsnprintf(nullptr, 0, str, argsList) + 1;
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
        va_start(argsList, level);
        npf_vsnprintf(&buffer[bufferStart], strLen, str, argsList);
        va_end(argsList);
        bufferStart += strLen;
        
        buffer[bufferStart++] = '\r';
        buffer[bufferStart++] = '\n';

        if (CoreLocalAvailable() && CoreLocal().logBuffers != nullptr)
        {
            CoreLogBuffer& localBuffer = *reinterpret_cast<CoreLogBuffer*>(CoreLocal().logBuffers);
            WriteLog(buffer, bufferLen, CoreLogBufferSize, localBuffer);
        }
        else
        {
            if (earlyBuffer.buffer == nullptr)
                earlyBuffer.buffer = earlyBufferStore;
            WriteLog(buffer, bufferLen, EarlyBufferSize, earlyBuffer);
        }
    }

    void InitCoreLogBuffers()
    {
        CoreLogBuffer* coreLog = new CoreLogBuffer();
        coreLog->head = coreLog->reserveHead = coreLog->lock = coreLog->eloLastHead = 0;
        
        uintptr_t bufferAddr = PMM::Global().Alloc(CoreLogBufferSize / PageSize) + hhdmBase;
        coreLog->buffer = reinterpret_cast<char*>(bufferAddr);

        CoreLocal().logBuffers = coreLog;
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
        ASSERT_UNREACHABLE();
    }

    void DetachLogDriver(size_t deviceId)
    {
        ASSERT_UNREACHABLE();
    }

    void LogWriterServiceMain(void*)
    {
        //TODO: if ELOs are active, flush those, otherwise update each driver with the new
        //read head.
    }

    void Panic(TrapFrame* exceptionFrame, const char* reason)
    {
        ASSERT_UNREACHABLE(); //lol
    }
}
