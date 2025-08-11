#pragma once

#include <Atomic.hpp>
#include <Compiler.hpp>

namespace sl
{
    template<typename LockType>
    class ScopedLock
    {
    private:
        LockType& lock;
        bool shouldUnlock;

    public:
        ScopedLock(LockType& l) : lock(l)
        {
            lock.Lock();
            shouldUnlock = true;
        }

        ~ScopedLock()
        {
            if (shouldUnlock)
                lock.Unlock();
        }
    
        ScopedLock(const ScopedLock&) = delete;
        ScopedLock& operator=(const ScopedLock&) = delete;
        ScopedLock(ScopedLock&&) = delete;
        ScopedLock& operator=(ScopedLock&&) = delete;

        inline void Release()
        {
            if (shouldUnlock)
                lock.Unlock();
            shouldUnlock = false;
        }
    };

    class SpinLock
    {
    private:
        constexpr static char LockedValue = 1;
        constexpr static char UnlockedValue = 0;

        sl::Atomic<char> lock;

    public:
        constexpr SpinLock() : lock(UnlockedValue)
        {}

        inline void Lock()
        {
            while (true)
            {
                char expected = UnlockedValue;
                if (lock.CompareExchange(expected, LockedValue, Acquire))
                    break;
                while (lock.Load(Acquire) == LockedValue)
                    HintSpinloop();
            }
        }

        inline bool TryLock()
        {
            char expected = UnlockedValue;
            return lock.CompareExchange(expected, LockedValue, Acquire);
        }

        inline void Unlock()
        {
            lock.Store(UnlockedValue, Release);
        }

        inline bool IsLocked()
        {
            return lock.Load(Relaxed);
        }
    };
}
