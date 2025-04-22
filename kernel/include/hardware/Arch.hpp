#pragma once

#include <Compiler.h>
#include <Types.h>
#include <Flags.h>
#include <Optional.h>

namespace Npk
{
    SL_ALWAYS_INLINE
    CpuId MyCoreId();

    SL_ALWAYS_INLINE
    uintptr_t MyCpuLocals();

    void SetMyLocals(uintptr_t where, CpuId softwareId);

    SL_ALWAYS_INLINE
    void WaitForIntr();

    SL_ALWAYS_INLINE
    bool IntrsExchange(bool on);

    SL_ALWAYS_INLINE
    bool IntrsOff()
    {
        return IntrsExchange(false);
    }

    SL_ALWAYS_INLINE
    bool IntrsOn()
    {
        return IntrsExchange(true);
    }

    struct InitState;

    void ArchInitEarly();
    uintptr_t ArchInitBspMmu(InitState& state);
    void ArchInit(InitState& state);

    SL_ALWAYS_INLINE
    size_t PfnShift();

    SL_ALWAYS_INLINE
    size_t PageSize()
    {
        return static_cast<size_t>(1) << PfnShift();
    }

    SL_ALWAYS_INLINE
    size_t PageMask()
    {
        return PageSize() - 1;
    }
    
    SL_ALWAYS_INLINE
    size_t KernelStackPages()
    {
        return 4;
    }

    SL_ALWAYS_INLINE
    size_t KernelStackSize()
    {
        return KernelStackPages() << PfnShift();
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

    enum class MmuFlag
    {
        Write,
        Fetch,
        User,
        Mmio,
        Framebuffer,
    };

    using MmuFlags = sl::Flags<MmuFlag>;

    using KernelMap = Paddr;

    void ArchEarlyMap(InitState& state, Paddr paddr, uintptr_t vaddr, MmuFlags flags);
    KernelMap ArchSetKernelMap(sl::Opt<KernelMap> next);
}

#ifdef __x86_64__
    #include <hardware/x86_64/Arch.hpp>
#else
    #error "Unsupported target architecture."
#endif
