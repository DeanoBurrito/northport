#pragma once

#include <Compiler.hpp>
#include <Types.hpp>
#include <Flags.hpp>
#include <Optional.hpp>
#include <Span.hpp>

namespace Npk
{
    SL_ALWAYS_INLINE
    CpuId MyCoreId();

    SL_ALWAYS_INLINE
    uintptr_t MyCpuLocals();

    using KernelMap = Paddr;

    void SetMyLocals(uintptr_t where, CpuId softwareId);

    struct TrapFrame;
    struct ArchThreadContext;
    struct ThreadContext;

    uintptr_t ArchGetTrapReturnAddr(const TrapFrame* frame);

    SL_ALWAYS_INLINE
    ThreadContext* GetCurrentThread();

    SL_ALWAYS_INLINE
    void SetCurrentThread(ThreadContext* context);

    void ArchSwitchThread(ArchThreadContext** current, ArchThreadContext* next) 
        asm("ArchSwitchThread");

    void ArchPrimeThread(ArchThreadContext** store, uintptr_t stub, uintptr_t entry, uintptr_t arg, uintptr_t stack);

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
    uintptr_t ArchInitBspMmu(InitState& state, size_t tempMapCount);
    void ArchInitDomain0(InitState& state);
    void ArchInitFull(uintptr_t& virtBase);

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

    enum class DebugEventType;
    enum class DebugStatus;

    SL_ALWAYS_INLINE
    DebugStatus ArchCallDebugger(DebugEventType type, void* data);

    enum class MmuFlag
    {
        Write,
        Fetch,
        User,
        Mmio,
        Framebuffer,
    };

    using MmuFlags = sl::Flags<MmuFlag>;

    enum class MmuError
    {
        Success = 0,
        InvalidArg,
        PageAllocFailed,
        NoMap,
        MapAlreadyExits
    };

    void ArchEarlyMap(InitState& state, Paddr paddr, uintptr_t vaddr, MmuFlags flags);
    KernelMap ArchSetKernelMap(sl::Opt<KernelMap> next);
    void* ArchSetTempMap(KernelMap* map, size_t index, Paddr paddr);
    MmuError ArchAddMap(KernelMap* map, uintptr_t vaddr, Paddr paddr, MmuFlags flags);
    void ArchFlushTlb(uintptr_t base, size_t length);

    /* Initializes any architecture state related to hardware debugging. This
     * runs on every cpu from within the debug event handler.
     * This function returns if debugging hardware initialized successfully.
     */
    bool ArchInitDebugState();

    /* Places a software breakpoint at the specified address, `backup` contains
     * a buffer where the overwritten code is stored for later restoration.
     * The number of bytes overwritten is returned, or 0 if an error occured.
     */
    size_t ArchSetBreakpoint(void* addr, sl::Span<uint8_t> backup);
}

#ifdef __x86_64__
    #include <hardware/x86_64/Arch.hpp>
#else
    #error "Unsupported target architecture."
#endif
