#pragma once

#include <core/Scheduler.h>
#include <Time.h>

namespace Npk::Core
{
    class WaitManager;
    struct Waitable;
    struct WaitControl;

    struct WaitEntry
    {
    friend WaitManager;
    private:
        WaitControl* control;
        char entryIndex;
        bool satisfied;

    public:
        sl::ListHook hook;
    };

    struct Waitable
    {
    friend WaitManager;
    private:
        sl::RunLevelLock<RunLevel::Dpc> lock;
        size_t count;
        size_t maxCount;
        sl::List<WaitEntry, &WaitEntry::hook> waiters;

    public:
        void Signal(size_t count);
        void Reset(size_t initialCount, size_t maxCount);
    };

    enum class WaitResult
    {
        Success,
        Timeout,
        Cancelled,
        Aborted,
    };

    class WaitManager
    {
    private:
        static bool LockAll(sl::Span<Waitable*> events);
        static void UnlockAll(sl::Span<Waitable*> events);
        static sl::Opt<WaitResult> TryFinish(WaitControl& control, bool waitAll);

    public:
        static WaitResult WaitOne();
        static WaitResult WaitMany(sl::Span<Waitable*> events, WaitEntry* entries, sl::ScaledTime timeout, bool waitAll);
        static void CancelWait(SchedulerObj* thread);
    };
}

using Npk::Core::WaitEntry;
