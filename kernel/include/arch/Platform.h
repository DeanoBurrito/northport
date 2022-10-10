#pragma once

#include <stdint.h>
#include <stddef.h>

namespace Npk
{
    struct TrapReturnFrame
    {
        uintptr_t key; //MUST be 1
        uintptr_t mapRoot;
        uintptr_t stack;
    };

    struct CoreLocalInfo
    {
        uintptr_t scratch[4]; //reserved for use during syscall/sysret/rv64 trap handlers.
        uintptr_t selfAddr;
        uintptr_t id;
        uintptr_t interruptControl; //x86: lapic*, rv: plic context id
        void* schedThread;
        void* vmm;
        void* nextKernelStack; //x86: duplicate of tss->rsp0
    };

    extern uintptr_t hhdmBase;
    extern uintptr_t hhdmLength;

    template<typename T>
    constexpr T AddHhdm(T value)
    { return reinterpret_cast<T>((uintptr_t)value + hhdmBase); }

    template<typename T>
    constexpr T SubHhdm(T value)
    { return reinterpret_cast<T>((uintptr_t)value - hhdmBase); }

    struct TrapFrame;

    void Wfi();
    bool InterruptsEnabled();
    void EnableInterrupts();
    void DisableInterrupts();
    void AllowSumac();
    void BlockSumac();

    void SetSystemTimer(size_t nanoseconds, void (*callback)(size_t));

    bool IsBsp();
    CoreLocalInfo& CoreLocal();
    bool CoreLocalAvailable();

    [[gnu::always_inline, noreturn]]
    inline void Halt()
    { 
        while (true) 
            Wfi(); 
        __builtin_unreachable();
    }

    struct InterruptGuard
    {
    private:
        bool prevState;
    public:
        InterruptGuard()
        {
            prevState = InterruptsEnabled();
            DisableInterrupts();
        }

        ~InterruptGuard()
        {
            if (prevState)
                EnableInterrupts();
        }
    };
}

#if defined(__x86_64__)
    #include <arch/x86_64/Platform.h>
#elif __riscv_xlen == 64
    #include <arch/riscv64/Platform.h>
#else
    #error "Compiling kernel for unsupoorted ISA."
#endif
