#pragma once

#include <stdint.h>
#include <stddef.h>
#include <Locks.h>

namespace Npk
{
    enum class RunLevel : uint8_t
    {
        Normal = 0,
        Dispatch = 1,
        IntHandler = 2,
    };
    
    struct CoreLocalInfo
    {
        uintptr_t scratch[4]; //reserved for use during syscall/sysret/rv64 trap handlers.
        uintptr_t selfAddr;
        uintptr_t id;
        uintptr_t interruptControl; //x86: lapic*, rv: plic context id
        RunLevel runLevel;
        void* schedThread;
        void* schedData;
        void* vmm;
        void* nextKernelStack; //x86: duplicate of tss->rsp0
    };

    extern uintptr_t hhdmBase;
    extern uintptr_t hhdmLength;

    template<typename T>
    inline T AddHhdm(T value)
    { return reinterpret_cast<T>((uintptr_t)value + hhdmBase); }

    template<>
    inline uintptr_t AddHhdm(uintptr_t value)
    { return value + hhdmBase; }

    template<typename T>
    inline T SubHhdm(T value)
    { return reinterpret_cast<T>((uintptr_t)value - hhdmBase); }

    template<>
    inline uintptr_t SubHhdm(uintptr_t value)
    { return value - hhdmBase; }

    struct TrapFrame;

    void Wfi();
    bool InterruptsEnabled();
    void EnableInterrupts();
    void DisableInterrupts();
    void AllowSumac();
    void BlockSumac();
    void HintSpinloop();

    void InitTrapFrame(TrapFrame* frame, uintptr_t stack, uintptr_t entry, void* arg, bool user);
    void ExecuteTrapFrame(TrapFrame* frame) asm("ExecuteTrapFrame");

    void SendIpi(size_t dest);
    uintptr_t MsiAddress(size_t core, size_t vector);
    uintptr_t MsiData(size_t core, size_t vector);

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

    class InterruptLock
    {
    private:
        bool restoreInts;
        sl::SpinLock lock;
        
    public:
        inline void Lock()
        {
            restoreInts = InterruptsEnabled();
            Npk::DisableInterrupts();
            lock.Lock();
        }

        inline void Unlock()
        {
            lock.Unlock();
            if (restoreInts)
                Npk::EnableInterrupts();
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
