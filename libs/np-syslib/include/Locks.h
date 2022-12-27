#pragma once

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
        char lock = 0;
    public:
        inline void Lock()
        {
            while (true)
            {
                if (!__atomic_test_and_set(&lock, __ATOMIC_ACQUIRE))
                    break;
                while (__atomic_load_n(&lock, __ATOMIC_ACQUIRE))
                {}
            }
        }

        inline void Unlock()
        {
            __atomic_clear(&lock, __ATOMIC_RELEASE);
        }
    };
    
    class TicketLock
    {
    private:
        unsigned serving = 0;
        unsigned next = 0;

    public:
        inline void Lock()
        {
            const unsigned ticket = __atomic_fetch_add(&next, 1, __ATOMIC_RELAXED);
            while (__atomic_load_n(&serving, __ATOMIC_ACQUIRE) != ticket)
            {}
        }

        inline void Unlock()
        {
            __atomic_add_fetch(&serving, 1, __ATOMIC_RELEASE);
        }
    };
}
