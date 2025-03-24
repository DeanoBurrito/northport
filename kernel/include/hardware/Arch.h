#pragma once

#include <core/RunLevels.h>
#include <Types.h>
#include <Compiler.h>
#include <Span.h>
#include <Error.h>
#include <Flags.h>
#include <Optional.h>

namespace Npk
{
    struct MemoryDomain;

    struct EarlyMmuEnvironment
    {
        uintptr_t directMapBase;
        size_t directMapLength;
        MemoryDomain* dom0;
        Paddr (*EarlyPmAlloc)();
    };

    void ArchEarlyEntry();
    void ArchMappingEntry(EarlyMmuEnvironment& env, uintptr_t& vmAllocHead);
    void ArchLateEntry();
    void ArchInitCore(size_t id);

    size_t ArchGetPerCpuSize();

    SL_ALWAYS_INLINE
    bool IntrsEnabled();

    SL_ALWAYS_INLINE
    bool DisableIntrs();

    SL_ALWAYS_INLINE
    bool EnableIntrs();

    SL_ALWAYS_INLINE
    void Wfi();

    [[noreturn]]
    SL_ALWAYS_INLINE
    void Halt()
    {
        while (true)
            Wfi();
        __builtin_unreachable();
    }

    SL_ALWAYS_INLINE
    bool CoreLocalAvailable();

    SL_ALWAYS_INLINE
    size_t CoreId();

    SL_ALWAYS_INLINE
    RunLevel CurrentRunLevel();

    SL_ALWAYS_INLINE
    RunLevel ExchangeRunLevel(RunLevel desired);

    SL_ALWAYS_INLINE
    Core::DpcQueue* LocalDpcs();

    SL_ALWAYS_INLINE
    Core::ApcQueue* LocalApcs();

    SL_ALWAYS_INLINE
    MemoryDomain& LocalDomain();

    enum class SubsysPtr
    {
        Logs = 0,
        IntrCtrl = 2,
        ClockQueue = 3,
        Thread = 4,
        Scheduler = 5,
        UnsafeOpAbort = 7,

        Count
    };

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

    size_t UnsafeCopy(void* dest, const void* source, size_t count) asm("UnsafeCopy");

    struct ExecFrame;

    //constructs an ExecFrame on the memory indicated by the `stack` variable
    ExecFrame* InitExecFrame(uintptr_t stack, uintptr_t entry, void* arg);

    //saves the current thread state (if `save` is non-null), switches stack to the one indicated
    //by `load`. If `callback` is non-null the callback function is called before restoring
    //the rest of the state indicated in `load`.
    void SwitchExecFrame(ExecFrame** save, ExecFrame* load, void (*callback)(void*), void* callbackArg) 
        asm("SwitchExecFrame");

    struct ExtendedRegs;

    void InitExtendedRegs(ExtendedRegs** regs);
    void SaveExtendedRegs(ExtendedRegs* regs);
    void LoadExtendedRegs(ExtendedRegs* regs);
    bool ExtendedRegsFence();

    size_t GetCallstack(sl::Span<uintptr_t> store, uintptr_t start, size_t offset = 0);
    void PoisonMemory(sl::Span<uint8_t> range);

    struct MmuSpace;

    struct MmuCapabilities
    {
        bool hwDirtyBit;
        bool hwAccessedBit;
    };

    enum class MmuError
    {
        Success = 0,
        InvalidArg,
        PmAllocFailed,
        MapAlreadyExists,
        NoExistingMap,
    };

    enum class MmuFlag
    {
        Write,
        Execute,
        User,
        Global,
        Mmio,
        Framebuffer,
    };

    struct MmuMapping
    {
        Paddr paddr;
        bool accessed;
        bool dirty;
    };

    using MmuFlags = sl::Flags<MmuFlag>;

    uintptr_t EarlyMmuBegin(const EarlyMmuEnvironment& env);
    void EarlyMmuMap(const EarlyMmuEnvironment& env, uintptr_t vaddr, uintptr_t paddr, MmuFlags flags);
    void EarlyMmuEnd(const EarlyMmuEnvironment& env);

    void LocalMmuInit();
    void GetMmuCapabilities(MmuCapabilities& caps);

    MmuSpace* CreateMmuSpace();
    void DestroyMmuSpace(MmuSpace** space);
    MmuError MmuMap(MmuSpace* space, void* vaddr, Paddr paddr, MmuFlags flags);
    sl::ErrorOr<MmuMapping, MmuError> MmuUnmap(MmuSpace* space, void* vaddr);
    sl::ErrorOr<MmuMapping, MmuError> MmuGetMap(MmuSpace* space, void* vaddr);
    MmuError MmuSetMap(MmuSpace* space, void* vaddr, sl::Opt<Paddr> paddr, sl::Opt<MmuFlags> flags);
    void MmuFlushCaches(MmuSpace* space, sl::Opt<void*> vaddr = {});
    void MmuActivate(MmuSpace* space, bool supervisor);
}

#ifdef __x86_64__
    #include <hardware/x86_64/Arch.h>
#else
    #error "Unsupported target architecture."
#endif
