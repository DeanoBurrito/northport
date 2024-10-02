#pragma once

#include <arch/__Select.h>
#include <stdint.h>
#include <stddef.h>
#include <Span.h>
#include <core/RunLevels.h>
#include <interfaces/intra/Compiler.h>

namespace Npk
{
    //each entry here is a place for the relevant subsystem to store core-local data
    enum class LocalPtr : size_t
    {
        Logs,
        Thread,
        IpiMailbox,
        IntrCtrl,
        ClockQueue,

        Count
    };

    //top-level core-local structure, the bulk of the data is stored via indirect
    //pointers in the subsystemPtrs[] array. Not ideal, but it prevents including
    //a number of other headers here, and polluting unrelated files by including
    //this one.
    struct CoreLocalInfo
    {
        void* scratch;
        void* nextStack;
        size_t id;
        RunLevel runLevel;
        Core::DpcQueue dpcs;
        Core::ApcQueue apcs;
        void* subsystemPtrs[(size_t)LocalPtr::Count];

        constexpr void*& operator[](LocalPtr index)
        { return subsystemPtrs[(size_t)index]; }
    };

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

    CoreLocalInfo& CoreLocal();
    bool CoreLocalAvailable();
}

#ifdef NPK_ARCH_INCLUDE_MISC
#include NPK_ARCH_INCLUDE_MISC
#endif
