#pragma once

#include <ArchHints.h>

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
        char lock;
    public:
        constexpr SpinLock() : lock(0)
        {}

        inline void Lock()
        {
            while (true)
            {
                if (!__atomic_test_and_set(&lock, __ATOMIC_ACQUIRE))
                    break;
                while (__atomic_load_n(&lock, __ATOMIC_ACQUIRE))
                    HintSpinloop();
            }
        }

        inline bool TryLock()
        {
            return !__atomic_test_and_set(&lock, __ATOMIC_ACQUIRE);
        }

        inline void Unlock()
        {
            __atomic_clear(&lock, __ATOMIC_RELEASE);
        }
    };
    
    class TicketLock
    {
    private:
        unsigned serving;
        unsigned next;

    public:
        constexpr TicketLock() : serving(0), next(0)
        {}

        inline void Lock()
        {
            const unsigned ticket = __atomic_fetch_add(&next, 1, __ATOMIC_RELAXED);
            while (__atomic_load_n(&serving, __ATOMIC_ACQUIRE) != ticket)
                HintSpinloop();
        }

        inline void Unlock()
        {
            __atomic_add_fetch(&serving, 1, __ATOMIC_RELEASE);
        }
    };

    class RwLock
    {
    private:
        unsigned writers { 0 };
        unsigned readers { 0 };
        sl::TicketLock lock {};
        
    public:
        inline void ReaderLock()
        {
            while (__atomic_load_n(&writers, __ATOMIC_ACQUIRE) != 0)
                HintSpinloop();

            lock.Lock();
            __atomic_add_fetch(&readers, 1, __ATOMIC_ACQUIRE);
            lock.Unlock();
        }

        inline void ReaderUnlock()
        {
            __atomic_sub_fetch(&readers, 1, __ATOMIC_RELEASE);
        }

        inline void WriterLock()
        {
            __atomic_add_fetch(&writers, 1, __ATOMIC_ACQUIRE);

            lock.Lock();
            
            while (__atomic_load_n(&readers, __ATOMIC_ACQUIRE) != 0)
                HintSpinloop();
        }

        inline void WriterUnlock()
        {
            __atomic_sub_fetch(&writers, 1, __ATOMIC_RELEASE);
            lock.Unlock();
        }
    };

#ifdef NP_KERNEL
} //close the namespace to prevent contamination

//These locks require the use of privileged functions only available in the northport kernel.
//By default these functions are only made available in kernel code.
//The following header is also relative to the kernel source, and will only resolve for the kernel.
//It will generate errors for other projects.
#include <arch/Platform.h>

namespace sl
{
    template<RunLevel CriticalLevel>
    class RunLevelLock
    {
    private:
        sl::Opt<RunLevel> prevLevel;
        sl::SpinLock lock;

    public:
        constexpr RunLevelLock() : prevLevel(), lock()
        {}

        inline void Lock()
        {
            if (Npk::CoreLocalAvailable())
                prevLevel = Npk::Tasking::EnsureRunLevel(CriticalLevel);
            return lock.Lock();
        }

        inline void Unlock()
        {
            lock.Unlock();
            if (Npk::CoreLocalAvailable() && prevLevel.HasValue())
                Npk::Tasking::LowerRunLevel(*prevLevel);
            prevLevel = {};
        }
    };
#endif
}
