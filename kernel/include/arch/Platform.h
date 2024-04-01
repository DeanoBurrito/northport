#pragma once

#include <stdint.h>
#include <stddef.h>
#include <tasking/RunLevels.h>

namespace Npk
{
    constexpr size_t NoCoreAffinity = -1ul;

    //each of the subsystems that get to store core-local data.
    enum class LocalPtr : size_t
    {
        Config, //core-specific settings, supported features etc
        IntControl, //interrupt controller data: lapic/imsic
        Scheduler, //core-local data for scheduler
        Thread, //currently running thread
        Vmm, //active *lower-half* VMM
        Log, //debug log buffer for this core
        HeapCache, //core-local caches to speed up heap interactions

        Count
    };

    struct CoreLocalInfo
    {
        uintptr_t scratch;
        uintptr_t id;
        uintptr_t acpiId;
        void* nextStack; //next stack available for kernel use. On x86_64 this is a duplicate of tss->rsp0
        RunLevel runLevel;
        sl::QueueMpSc<Tasking::Dpc> dpcs;
        sl::QueueMpSc<Tasking::Apc> apcs;
        void* subsystemPtrs[(size_t)LocalPtr::Count];

        constexpr void*& operator[](LocalPtr index)
        { return subsystemPtrs[(size_t)index]; }
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
    struct ExtendedRegs;

    void Wfi();
    bool InterruptsEnabled();
    void EnableInterrupts();
    void DisableInterrupts();
    void AllowSumac();
    void BlockSumac();

    void InitTrapFrame(TrapFrame* frame, uintptr_t stack, uintptr_t entry, bool user);
    void SetTrapFrameArg(TrapFrame* frame, size_t index, void* value);
    void* GetTrapFrameArg(TrapFrame* frame, size_t index);

    void InitExtendedRegs(ExtendedRegs** regs);
    void SaveExtendedRegs(ExtendedRegs* regs);
    void LoadExtendedRegs(ExtendedRegs* regs);
    void ExtendedRegsFence();

    uintptr_t GetReturnAddr(size_t level);
    void SendIpi(size_t dest);
    void SetHardwareRunLevel(RunLevel rl);
    uintptr_t MsiAddress(size_t core, size_t vector);
    uintptr_t MsiData(size_t core, size_t vector);
    void MsiExtract(uintptr_t addr, uintptr_t data, size_t& core, size_t& vector);

    void SwitchFrame(TrapFrame** prev, TrapFrame* next) asm("SwitchFrame");
    void Panic(const char* reason) asm("Panic");

    CoreLocalInfo& CoreLocal();
    bool CoreLocalAvailable();

    [[gnu::always_inline, noreturn]]
    inline void Halt()
    { 
        while (true) 
            Wfi(); 
        __builtin_unreachable();
    }
}

#if defined(__x86_64__)
    #include <arch/x86_64/Platform.h>
#elif __riscv_xlen == 64
    #include <arch/riscv64/Platform.h>
#else
    #error "Compiling kernel for unsupoorted ISA."
#endif
