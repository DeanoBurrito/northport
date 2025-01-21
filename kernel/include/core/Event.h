#pragma once

#include <core/Scheduler.h>
#include <Time.h>

namespace Npk::Core
{
    constexpr sl::TimeCount NoTimeout = sl::TimeCount(-1, -1);

    class WaitManager;
    struct Waitable;
    struct WaitControl;

    struct WaitEntry
    {
    friend Waitable;
    friend WaitManager;
    private:
        WaitControl* control;

    public:
        sl::ListHook hook;
        bool satisfied;
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
        void Signal(size_t amount);
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
        static WaitResult WaitOne(Waitable* event, WaitEntry* entry, sl::TimeCount timeout);
        static WaitResult WaitMany(sl::Span<Waitable*> events, WaitEntry* entries, sl::TimeCount timeout, bool waitAll);
        static void CancelWait(SchedulerObj* thread);
    };

    SL_ALWAYS_INLINE
    WaitResult WaitOne(Waitable* event, WaitEntry* entry, sl::TimeCount timeout)
    { return WaitManager::WaitOne(event, entry, timeout); }
    
    SL_ALWAYS_INLINE
    WaitResult WaitMany(sl::Span<Waitable*> events, WaitEntry* entries, sl::TimeCount timeout, bool waitAll)
    { return WaitManager::WaitMany(events, entries, timeout, waitAll); }

    SL_ALWAYS_INLINE
    void CancelWait(SchedulerObj* thread)
    { return WaitManager::CancelWait(thread); }
}

using Npk::Core::WaitEntry;
