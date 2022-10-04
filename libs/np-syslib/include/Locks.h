#pragma once

namespace sl
{
    template<typename LockType>
    class ScopedLock
    {
    private:
        LockType& lock;

    public:
        ScopedLock(LockType& l) : lock(l)
        {
            lock.Lock();
        }

        ~ScopedLock()
        {
            lock.Unlock();
        }
    
        ScopedLock(const ScopedLock&) = delete;
        ScopedLock& operator=(const ScopedLock&) = delete;
        ScopedLock(ScopedLock&&) = delete;
        ScopedLock& operator=(ScopedLock&&) = delete;
    };

    class SpinLock
    {
    private:
        char lock = 0;
    public:
        inline void Lock()
        {
            while (__atomic_test_and_set(&lock, __ATOMIC_ACQUIRE));
        }

        inline void Unlock()
        {
            __atomic_clear(&lock, __ATOMIC_RELEASE);
        }
    };
    
    class TicketLock
    {
    private:
        unsigned long serving = 0;
        unsigned long next = 0;

    public:
        inline void Lock()
        {
            const unsigned long ticket = __atomic_fetch_add(&next, 1, __ATOMIC_ACQUIRE);
            while (__atomic_load_n(&serving, __ATOMIC_ACQUIRE) != ticket);
        }

        inline void Unlock()
        {
            __atomic_add_fetch(&serving, 1, __ATOMIC_RELEASE);
        }
    };
}
