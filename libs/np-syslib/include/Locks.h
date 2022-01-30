#pragma once

namespace sl
{
    [[gnu::always_inline]]
    inline void SpinlockAcquire(void* lock)
    {
        while (__atomic_test_and_set(lock, __ATOMIC_ACQUIRE));
    }

    [[gnu::always_inline]]
    inline void SpinlockRelease(void* lock)
    {
        __atomic_clear(lock, __ATOMIC_RELEASE);
    }

    class ScopedSpinlock
    {
    private:
        void* lock;
    public:
        ScopedSpinlock() = delete;
        ScopedSpinlock(void* ptr)
        {
            lock = ptr;
            SpinlockAcquire(lock);
        }

        ~ScopedSpinlock()
        { SpinlockRelease(lock); }

        ScopedSpinlock(const ScopedSpinlock&) = delete;
        ScopedSpinlock& operator=(const ScopedSpinlock&) = delete;
        ScopedSpinlock(ScopedSpinlock&&) = delete;
        ScopedSpinlock& operator=(ScopedSpinlock&&) = delete;
    };
}
