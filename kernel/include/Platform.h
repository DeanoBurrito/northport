#pragma once

#include <stdint.h>
#include <Cpu.h>

#define PAGE_FRAME_SIZE 0x1000
#define PORT_DEBUGCON 0xE9
#define MSR_IA32_EFER 0xC0000080
#define MSR_FS_BASE 0xC0000100
#define MSR_GS_BASE 0xC0000101
#define MSR_GS_KERNEL_BASE 0xC0000102

#define GDT_ENTRY_RING_0_CODE 0x8
#define GDT_ENTRY_RING_0_DATA 0x10
#define GDT_ENTRY_RING_3_CODE 0x18
#define GDT_ENTRY_RING_3_DATA 0x20

#define FORCE_INLINE [[gnu::always_inline]] inline

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

    FORCE_INLINE uint64_t ReadCR0()
    {
        uint64_t value;
        asm volatile("mov %%cr0, %0" : "=r"(value));
        return value;
    }

    FORCE_INLINE void WriteCR0(uint64_t value)
    {
        asm volatile("mov %0, %%cr0" :: "r"(value));
    }

    FORCE_INLINE uint64_t ReadCR2()
    {
        uint64_t value;
        asm volatile("mov %%cr2, %0" : "=r"(value));
        return value;
    }

    FORCE_INLINE uint64_t ReadCR3()
    {
        uint64_t value;
        asm volatile("mov %%cr3, %0" : "=r"(value));
        return value;
    }

    FORCE_INLINE void WriteCR3(uint64_t value)
    {
        asm volatile("mov %0, %%cr3" :: "r"(value));
    }

    FORCE_INLINE uint64_t ReadCR4()
    {
        uint64_t value;
        asm volatile("mov %%cr4, %0" : "=r"(value));
        return value;
    }

    FORCE_INLINE void WriteCR4(uint64_t value)
    {
        asm volatile("mov %0, %%cr4" :: "r"(value));
    }

    struct CoreLocalStorage;

    FORCE_INLINE CoreLocalStorage* GetCoreLocal()
    { return reinterpret_cast<CoreLocalStorage*>(CPU::ReadMsr(MSR_GS_BASE)); }
}
