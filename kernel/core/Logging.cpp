#include <Core.hpp>
#include <Memory.hpp>
#include <NanoPrintf.hpp>
#include <Maths.hpp>

/* Logging thoughts and implementation notes:
 * This logging subsystem is quite simple. Each log message ('item') is a fixed-sized
 * buffer of `MaxLogLength` bytes with a header prepended to it. The header contains
 * info like which module authored the log, when it was written, the log level etc.
 * Log items are organised into a pair of global lists, one list for unused log items
 * and one logs pending writeout. These lists are MPSC and are waitfree on the producer
 * side. This is perfect for the pending logs list, since only a single thread can
 * hold the lock for the logging outputs. 
 * **For now** the free list also uses an MPSC queue, which is not ideal. When I
 * get around to implementing an SPMC queue we'll use this, for the inverse of above.
 * 
 * Writing log items to active log outputs can happen in a few ways, depending on
 * the value of `selfDrainLogs`. This flag starts set and is cleared by a kernel
 * worker thread spawned later in the boot process. When `selfDrainLogs` is set,
 * the thread/core that has just appended to the pending logs list will try to acquire
 * the lock for the log outputs and write-out `MaxSelfDrainCount` items. The limit
 * exists so that one cpu doesn't get stuck writing logs for the other cpus while
 * not progressing it's own work.
 * When `selfDrainLogs` is clear, log write-out is handled by the worker thread.
 */
namespace Npk
{
    constexpr size_t InitialLogItems = 128;
    constexpr size_t MaxLogLength = 128;
    constexpr size_t MaxSelfDrainCount = 64;

    struct QueuedLogData
    {
        LogSinkMessage details;
        char data[MaxLogLength];
    };

    using LogQueue = sl::QueueMpSc<QueuedLogData>;
    using LogQueueItem = LogQueue::Item;

    static LogQueueItem itemsStore[InitialLogItems];
    static LogQueue pendingLogs;

    static sl::Atomic<bool> selfDrainLogs = true;
    static bool initialized = false;

    static IntrSpinLock writeoutLock; //TODO: disabling preemption is enough, IplSpinLock<Dpc> should do the trick
    static sl::List<LogSink, &LogSink::listHook> logSinks; //protected by writeout lock
    static LogQueue freeLogs; //protected by writeout lock

    static LogQueueItem* AllocLogItem()
    {
        sl::ScopedLock scopeLock(writeoutLock);
        if (!initialized)
        {
            initialized = true;

            for (size_t i = 0; i < InitialLogItems; i++)
                freeLogs.Push(&itemsStore[i]);
        }

        //TODO: become NMI-safe
        return freeLogs.Pop();
    }

    //NOTE: assumes writeoutLock is held
    static void FreeLogItem(LogQueueItem* item)
    {
        if (item != nullptr)
            freeLogs.Push(item);
    }

    static void InsertLogItem(LogQueueItem* item)
    {
        sl::AtomicThreadFence(sl::Release);
        pendingLogs.Push(item);
    }

    static void TryWriteoutLogItems(size_t limit)
    {
        if (!writeoutLock.TryLock())
            return;

        for (size_t i = 0; i < limit; i++)
        {
            auto item = pendingLogs.Pop();
            if (item == nullptr)
                break;
            
            for (auto it = logSinks.Begin(); it != logSinks.End(); ++it)
                it->Write(item->data.details);

            FreeLogItem(item);
        }

        writeoutLock.Unlock();
    }

    void Log(const char* message, LogLevel level, ...)
    {
        auto logItem = AllocLogItem();
        NPK_ASSERT(logItem != nullptr);

        auto& details = logItem->data.details;
        details.level = level;
        details.when = GetMonotonicTime();
        details.who = "kernel";
        details.cpu = MyCoreId();

        va_list args;
        va_start(args, level);
        const size_t length = npf_vsnprintf(logItem->data.data, MaxLogLength, message, args);
        va_end(args);

        const size_t realLength = sl::Min(length, MaxLogLength - 1);
        details.text = sl::StringSpan(logItem->data.data, realLength);

        InsertLogItem(logItem);
        if (selfDrainLogs)
            TryWriteoutLogItems(MaxSelfDrainCount);
    }

    void AddLogSink(LogSink& sink)
    {
        writeoutLock.Lock();
        logSinks.PushBack(&sink);
        writeoutLock.Unlock();

        if (sink.Reset != nullptr)
            sink.Reset();
    }

    void RemoveLogSink(LogSink& sink)
    {
        writeoutLock.Lock();
        logSinks.Remove(&sink);
        writeoutLock.Unlock();
    }
}
