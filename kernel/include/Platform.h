#pragma once

#include <stdint.h>
#include <Cpu.h>
#include <NativePtr.h>

#define PAGE_FRAME_SIZE 0x1000

#define PORT_DEBUGCON 0xE9
#define PORT_PS2_DATA 0x60
#define PORT_PS2_COMMAND_STATUS 0x64

#define INTERRUPT_GSI_SPURIOUS 0xFF
#define INTERRUPT_GSI_PS2KEYBOARD 0x21
//NOTE: scheduler has this hardcoded in Yield() -> update it there if modifying
#define INTERRUPT_GSI_SCHEDULER_NEXT 0x22

#define MSR_IA32_EFER 0xC0000080
#define MSR_APIC_BASE 0x1B
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

    //if any values are pinned at (uint32_t)-1, they're invalid
    struct CpuFrequencies
    {
        uint32_t coreClockBaseHertz;
        uint32_t coreMaxBaseHertz;
        uint32_t coreTimerHertz; //TSC freq on x86
        uint32_t busClockHertz;
    };

    enum CoreLocalIndices : size_t
    {
        LAPIC,
        Scheduler,
        
        EnumCount,
    };

    struct CoreLocalStorage
    {
        uint64_t apicId;
        uint64_t acpiProcessorId;
        sl::NativePtr ptrs[CoreLocalIndices::EnumCount];
    };

    FORCE_INLINE CoreLocalStorage* GetCoreLocal()
    { return reinterpret_cast<CoreLocalStorage*>(CPU::ReadMsr(MSR_GS_BASE)); }

    struct [[gnu::packed]] StoredRegisters
    {
        uint64_t r15;
        uint64_t r14;
        uint64_t r13;
        uint64_t r12;
        uint64_t r11;
        uint64_t r10;
        uint64_t r9;
        uint64_t r8;
        uint64_t rbp;
        uint64_t rsp; //just a dummy value so its an even 16 regs - use iret_rsp for stack access
        uint64_t rdi;
        uint64_t rsi;
        uint64_t rdx;
        uint64_t rcx;
        uint64_t rbx;
        uint64_t rax;

        uint64_t vectorNumber;
        uint64_t errorCode;

        uint64_t iret_rip;
        uint64_t iret_cs;
        uint64_t iret_flags;
        uint64_t iret_rsp;
        uint64_t iret_ss;
    };
}
