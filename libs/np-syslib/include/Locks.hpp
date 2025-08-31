#pragma once

#include <Atomic.hpp>
#include <Compiler.hpp>
#include <Types.hpp>

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

    template<typename T, typename LockType, LockType& (*GetLock)(T*)>
    class ScopedMultiLock
    {
    private:
        T* objs;
        size_t objCount;

    public:
        ScopedMultiLock(T* objects, size_t objectCount)
        {
            objs = objects;
            objCount = objectCount;

            if (objCount == 0)
                return;

            size_t lockedCount = 0;
            uintptr_t lastLocked = 0;
            while (lockedCount!= objCount - 1)
            {
                size_t candidate = 0;
                auto candidateAddr = static_cast<uintptr_t>(~0);
                for (size_t i = 0; i < objCount; i++)
                {
                    const auto addr = reinterpret_cast<uintptr_t>(&objs[i]);

                    if (addr <= lastLocked)
                        continue;
                    if (addr > candidateAddr)
                        continue;

                    candidateAddr = addr;
                    candidate = i;
                }

                GetLock(&objs[candidate]).Lock();
                lastLocked = candidateAddr;
                lockedCount++;
            }
        }

        ~ScopedMultiLock()
        {
            Release();
        }

        ScopedMultiLock(const ScopedMultiLock&) = delete;
        ScopedMultiLock& operator=(const ScopedMultiLock&) = delete;
        ScopedMultiLock(ScopedMultiLock&&) = delete;
        ScopedMultiLock& operator=(ScopedMultiLock&&) = delete;

        inline void Release()
        {
            for (size_t i = 0; i < objCount; i++)
                GetLock(&objs[i]).Unlock();

            objCount = 0;
            objs = nullptr;
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
