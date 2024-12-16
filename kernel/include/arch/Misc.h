#pragma once

#include <arch/__Select.h>
#include <stdint.h>
#include <stddef.h>
#include <Span.h>
#include <core/RunLevels.h>
#include <interfaces/intra/Compiler.h>

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

        Count
    };

    ALWAYS_INLINE
    bool CoreLocalAvailable();

    ALWAYS_INLINE
    size_t CoreLocalId();

    ALWAYS_INLINE
    RunLevel CurrentRunLevel();

    ALWAYS_INLINE
    void SetRunLevel(RunLevel rl);

    ALWAYS_INLINE
    Core::DpcQueue* CoreLocalDpcs();

    ALWAYS_INLINE
    Core::ApcQueue* CoreLocalApcs();

    ALWAYS_INLINE
    void* GetLocalPtr(SubsysPtr which);

    ALWAYS_INLINE
    void SetLocalPtr(SubsysPtr which, void* data);

    ALWAYS_INLINE
    size_t PfnShift();

    ALWAYS_INLINE
    size_t PageSize()
    {
        return 1 << PfnShift();
    }

    ALWAYS_INLINE
    uintptr_t PageMask()
    {
        return (1 << PfnShift()) - 1;
    }

    template<typename T>
    ALWAYS_INLINE
    T AlignUpPage(T value)
    {
        const uintptr_t addr = reinterpret_cast<uintptr_t>(value);
        return reinterpret_cast<T>((addr + PageSize()) & ~PageMask());
    }

    template<typename T>
    ALWAYS_INLINE
    T AlignDownPage(T value)
    {
        const uintptr_t addr = reinterpret_cast<uintptr_t>(value);
        return reinterpret_cast<T>(addr & ~PageMask());
    }

    void ExplodeKernelAndReset();

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
