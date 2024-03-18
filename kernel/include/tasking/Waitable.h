#pragma once

#include <Locks.h>
#include <containers/List.h>
#include <Time.h>

namespace Npk::Tasking
{
    struct Thread;
    struct Waitable;
    class WaitManager;

    struct WaitEntry 
    {
    friend WaitManager;
    friend sl::IntrFwdList<WaitEntry>;
    private:
        WaitEntry* next;
        Waitable* event;
        Thread* thread;
        sl::Span<WaitEntry> cohort;
        bool signalled;
        bool waitAll; //TODO: storing per-thread control data in *each* wait entry is quite wasteful lol

    public:
        [[gnu::always_inline]]
        inline bool Signalled()
        { return signalled; }
    };

    struct Waitable
    {
    friend WaitManager;
    private:
        sl::SpinLock lock;
        sl::IntrFwdList<WaitEntry> waiters;
        size_t signalCount;

    public:
        size_t Signal(size_t count = 1);
    };

    class WaitManager
    {
    private:
        static bool TryFinish(sl::Span<WaitEntry> entries,  bool waitAll);
        static void LockAll(sl::Span<WaitEntry> entries);
        static void UnlockAll(sl::Span<WaitEntry> entries);

    public:
        inline static bool WaitOne(Waitable* event, WaitEntry* entry, sl::ScaledTime timeout)
        { return WaitMany({ event, 1 }, { entry, 1 }, timeout, false) == 1; }

        static size_t WaitMany(sl::Span<Waitable> events, sl::Span<WaitEntry> entries, sl::ScaledTime timeout, bool waitAll);
        static void CancelWait(Thread* thread);
        static size_t Signal(Waitable* event, size_t count = 1);
    };
}

namespace Npk
{
    constexpr auto WaitOne = Tasking::WaitManager::WaitOne;
    constexpr auto WaitMany = Tasking::WaitManager::WaitMany;
    constexpr auto CancelWait = Tasking::WaitManager::CancelWait;
}

