#pragma once

#include <arch/__Select.h>
#include <core/RunLevels.h>
#include <Span.h>
#include <Compiler.h>

namespace Npk
{
    /* Core-local info block: the layout of this is left to the arch-layer implementation,
     * instead we provide generic accessor functions that hopefully allow for an effiecient
     * and race-free inplementation.
     */
    enum class SubsysPtr
    {
        Logs = 0,
        IpiMailbox = 1,
        IntrCtrl = 2,
        ClockQueue = 3,
        Thread = 4,
        Scheduler = 5,
        PmmCache = 6,
        UnsafeOpAbort = 7,

        Count
    };

    SL_ALWAYS_INLINE
    bool CoreLocalAvailable();

    SL_ALWAYS_INLINE
    size_t CoreLocalId();

    SL_ALWAYS_INLINE
    RunLevel CurrentRunLevel();

    SL_ALWAYS_INLINE
    void SetRunLevel(RunLevel rl);

    SL_ALWAYS_INLINE
    Core::DpcQueue* CoreLocalDpcs();

    SL_ALWAYS_INLINE
    Core::ApcQueue* CoreLocalApcs();

    SL_ALWAYS_INLINE
    void* GetLocalPtr(SubsysPtr which);

    SL_ALWAYS_INLINE
    void SetLocalPtr(SubsysPtr which, void* data);

    SL_ALWAYS_INLINE
    size_t PfnShift();

    SL_ALWAYS_INLINE
    size_t KernelStackSize();

    SL_ALWAYS_INLINE
    size_t PageSize()
    {
        return 1 << PfnShift();
    }

    SL_ALWAYS_INLINE
    uintptr_t PageMask()
    {
        return (1 << PfnShift()) - 1;
    }

    template<typename T>
    SL_ALWAYS_INLINE
    T AlignUpPage(T value)
    {
        const uintptr_t addr = reinterpret_cast<uintptr_t>(value);
        return reinterpret_cast<T>((addr + PageMask()) & ~PageMask());
    }

    template<typename T>
    SL_ALWAYS_INLINE
    T AlignDownPage(T value)
    {
        const uintptr_t addr = reinterpret_cast<uintptr_t>(value);
        return reinterpret_cast<T>(addr & ~PageMask());
    }

    void ExplodeKernelAndReset();
    size_t UnsafeCopy(void* dest, const void* source, size_t count) asm("UnsafeCopy");

    //this struct represents any per-thread state not included in the TrapFrame.
    //This is generally things the kernel doesnt use, and is only saved/restored 
    //between user thread changes. This is just floating point and vector regs,
    //and maybe a few misc arch specific regs.
    struct ExtendedRegs;

    void InitExtendedRegs(ExtendedRegs** regs);
    void SaveExtendedRegs(ExtendedRegs* regs);
    void LoadExtendedRegs(ExtendedRegs* regs);
    bool ExtendedRegsFence();

    size_t GetCallstack(sl::Span<uintptr_t> store, uintptr_t start, size_t offset = 0);
    void PoisonMemory(sl::Span<uint8_t> range);
}

#ifdef NPK_ARCH_INCLUDE_MISC
#include NPK_ARCH_INCLUDE_MISC
#endif
