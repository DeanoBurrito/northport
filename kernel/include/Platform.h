#pragma once

#define PAGE_SIZE 0x1000
#define PORT_DEBUGCON 0xE9

#define FORCE_INLINE __attribute__((always_inline)) inline

namespace Kernel
{
    FORCE_INLINE void SpinlockAcquire(void* lock)
    {
        while (__atomic_test_and_set(lock, __ATOMIC_ACQUIRE));
    }

    FORCE_INLINE void SpinlockRelease(void* lock)
    {
        __atomic_clear(lock, __ATOMIC_RELEASE);
    }
}
