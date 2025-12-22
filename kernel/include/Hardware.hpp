#pragma once

#include <Optional.hpp>
#include <Span.hpp>
#include <Flags.hpp>
#include <Time.hpp>
#include <Memory.hpp>

namespace Npk
{
    /* Opaque type, **but** must be copyable. Represents the root of an address
     * translation map set. Since this is (so far) always a paddr pointing to
     * the root table we've defined it as a `Paddr`.
     */
    using HwMap = Paddr;

    /* Opaque type representing a single page table entry.
     */
    struct HwPte;

    /* Opaque type. Represents stored register state before a synchronous
     * exception/trap was fired.
     */
    struct TrapFrame;

    /* Opaque type. Represents arch-specific thread state, enough to save
     * and restore a kernel thread's state. Extended state used by userspace
     * is not required to be part of this structure.
     */
    struct HwThreadContext;

    /* Opaque type. Represents arch-specific state required for a thread to
     * enter user mode.
     */
    struct HwUserContext;

    /* Describes the major reason for an exit from a user mode context.
     */
    enum class HwUserExitType
    {
        InvalidEntryState = 0,
        Exit = 1,
        CpuException = 2,
        Syscall = 3,
    };

    /* Provides a complete description of why a user context returned to the 
     * kernel.
     */
    struct HwUserExitInfo
    {
        HwUserExitType type;
        size_t subtype;
    };

    /* Forward declaration, see Core.hpp for the full description.
     */
    struct ThreadContext;

    /* Forward declaration, see Debugger.hpp for the full description.
     */
    enum class DebugEventType;

    /* Forward declaration, see Debugger.hpp for the full description.
     */
    enum class DebugStatus;

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

    struct MmuWalkResult
    {
        /* Opaque pointer to a specific page table entry. If non-null, this can
         * be queried and modified via `HwPte...()` functions.
         */
        HwPte* pte;
        
        /* 0-based distance from the smallest granularity.
         * For example on x86_64 by default there are 4 levels, the lowest 3
         * of which can be translations. We would number these as 0
         * (4K translations, the last possible level), 1 (2M translations),
         * 2 (1G translations), and 3 (top level, no translations allowed).
         */
        uint8_t level;
        
        /* Whether this walk would be considered complete by the mmu,
         * and would map some area of physical memory.
         */
        bool complete;
    };

    /*
     */
    struct HwBreakpoint
    {
        uint8_t backup[6];
        uint8_t backupLength;
        uint8_t bind;
    };

    /* Calls `func` passing `a`/`b`/`c` as params to it, optionally placing
     * the return value of `func` into `*r` if non-null. If a synchronous
     * excepton (i.e. one occuring due to an instruction `func` executed)
     * this function aborts further execution of `func` and returns true
     * (an exception occured), the value of `*r` is left unchanged if non-null. 
     * If `func` returned normally, this function returns false.
     */
    extern "C"
    bool ExceptionAwareCall(void* a, void* b, void* c, void** r, 
        void* (*func)(void* a, void* b, void* c));

    /* Sugar function for calling `sl::MemCopy()` via `ExceptionAwareCall()`.
     */
    SL_ALWAYS_INLINE
    bool MemCopyExceptionAware(void* dest, const void* src, size_t len)
    {
        using FuncType = void* (*)(void*, void*, void*);

        void* inA = dest;
        void* inB = const_cast<void*>(src);
        void* inC = reinterpret_cast<void*>(len);

        const auto addr = reinterpret_cast<uintptr_t>(sl::MemCopy);
        auto func = reinterpret_cast<FuncType>(addr);

        return ExceptionAwareCall(inA, inB, inC, nullptr, func);
    }

    /* Returns the (kernel-assigned) unique id of the current cpu core.
     */
    SL_ALWAYS_INLINE
    CpuId MyCoreId();

    /* Returns the base address of the cpu-local storage block for the current
     * cpu core.
     */
    SL_ALWAYS_INLINE
    uintptr_t MyCpuLocals();

    /* Returns the program counter value stored in `frame`.
     */
    uintptr_t GetTrapReturnAddr(const TrapFrame* frame);

    /* Returns the stack pointer value stored in `frame`.
     */
    uintptr_t GetTrapStackPtr(const TrapFrame* frame);

    /* Returns the stack base pointer stored in `frame`.
     */
    uintptr_t GetTrapBasePtr(const TrapFrame* frame);

    /* Returns whether `frame` represents a user context (true), or a kernel
     * context (false).
     */
    bool GetTrapIsUserContext(const TrapFrame* frame);

    /* Returns a pointer to the context of the currently executing thread.
     */
    SL_ALWAYS_INLINE
    ThreadContext* GetCurrentThread();

    /* Sets the per-cpu pointer for the current thread to `context`.
     * This is given special treatment compared to other per-cpu variables,
     * as it may be accessed via assembly routines, and may need to be
     * accessed in a specific way.
     */
    SL_ALWAYS_INLINE
    void SetCurrentThread(ThreadContext* context);

    /* Execute a context switch. This function stores hardware-specific state
     * that represents this thread to `*current`, and loads the next thread's
     * context from `next`.
     * `current` may be a nullptr, in which case the current context is not
     * saved, meaning the current thread cannot be resumed in the future.
     *
     * Calling this function *will* relinquish control of the cpu.
     */
    extern "C"
    void HwSwitchThread(HwThreadContext** current, HwThreadContext* next);

    /* Creates and initializes a thread's hardware context. Setting the stack
     * pointer to `stack`, and priming the context to jump to a kernel entry
     * stub at `stub`, which will then call `entry` with `arg` as a parameter.
     *
     * The address of the initialized context is placed at `*store`.
     */
    void HwPrimeThread(HwThreadContext** store, uintptr_t stub, uintptr_t entry,
        uintptr_t arg, uintptr_t stack);

    /* Initializes a user mode context, typically to be used for the current
     * thread, but no such binding is enforced.
     */
    void HwPrimeUserContext(HwUserContext* context, uintptr_t entry, 
        uintptr_t arg, uintptr_t stack);

    /* Cleans up and releases any resources the harware layer may have attached
     * to a user context. This function is destructive and renders the user 
     * context unusable without another call to `HwPrimeUserContext()`.
     */
    void HwCleanupUserContext(HwUserContext* context);

    /* This function places the current cpu in user mode and transfers control
     * to the code specified in the user context. This function will eventually
     * return when this thread's user code causes an event requiring the kernel
     * to take action. Some events may not cause this function to return, and
     * instead route to other subsystems, like a page fault. Page faults are
     * routed to the virtual memory subsystem initially, and are only cause
     * for a usermode exit (meaning this function returns) if the fault was bad.
     *
     * Other exit events include system calls and synchronous cpu exceptions.
     * Asynchronous exceptions and device interrupts do not cause UM exits, as
     * they have no relation to the currently executing code.
     */
    HwUserExitInfo HwEnterUserContext(HwUserContext* context);

    /* Halts (or at least stalls) the current cpu core until an interrupt
     * fires. This should ideally put the cpu into a low(er) power state.
     */
    SL_ALWAYS_INLINE
    void WaitForIntr();

    /* Enables interrupts if `on` is set, disables them otherwise.
     * Returns whether interrupts were previously enabled.
     */
    SL_ALWAYS_INLINE
    bool IntrsExchange(bool on);

    /* Disables interrupts, returns whether interrupts were previously enabled.
     */
    SL_ALWAYS_INLINE
    bool IntrsOff()
    {
        return IntrsExchange(false);
    }

    /* Enales interrupts, returns whether interrupts were previously enabled.
     */
    SL_ALWAYS_INLINE
    bool IntrsOn()
    {
        return IntrsExchange(true);
    }

    /* Returns the system page size, as a power of 2.
     */
    SL_ALWAYS_INLINE
    size_t PfnShift();

    /* Returns the current system page size, in bytes.
     */
    SL_ALWAYS_INLINE
    size_t PageSize()
    {
        return static_cast<size_t>(1) << PfnShift();
    }

    /* Returns a bitmask that extracts the page-offset from an address.
     */
    SL_ALWAYS_INLINE
    size_t PageMask()
    {
        return PageSize() - 1;
    }
    
    /* Returns the size of kernel stacks, in pages.
     */
    SL_ALWAYS_INLINE
    size_t KernelStackPages()
    {
        return 4;
    }

    /* Returns the size of kernel stacks, in bytes.
     */
    SL_ALWAYS_INLINE
    size_t KernelStackSize()
    {
        return KernelStackPages() << PfnShift();
    }

    /* Align `value` up to the next page boundary.
     */
    template<typename T>
    SL_ALWAYS_INLINE
    T AlignUpPage(T value)
    {
        const uintptr_t addr = reinterpret_cast<uintptr_t>(value);
        return reinterpret_cast<T>((addr + PageMask()) & ~PageMask());
    }

    /* Align `value` down to the next page boundary.
     */
    template<typename T>
    SL_ALWAYS_INLINE
    T AlignDownPage(T value)
    {
        const uintptr_t addr = reinterpret_cast<uintptr_t>(value);
        return reinterpret_cast<T>(addr & ~PageMask());
    }

    /* Calls into the debugger core, notifying it of an event or request.
     * `type` defines the event/request type, and `data` is a pointer to an
     * event args struct (or nullptr, if the vent defines none).
     * Event arg structs are named after their associated event: an event
     * of type `Breakpoint` would pass the address of a `BreakpointEventArg`
     * struct as `data`.
     */
    SL_ALWAYS_INLINE
    DebugStatus HwCallDebugger(DebugEventType type, void* data);

    /* Set the physical address mapped by a temporary map slot at `index`.
     * Returns nullptr on error, or the virtual address `paddr` can be found at.
     */
    void* HwSetTempMapSlot(size_t index, Paddr paddr);

    /* Flush the local TLB for virtual addresses in `base` -> `base + length`
     */
    void HwFlushTlb(uintptr_t base, size_t length);

    /* Sets the current kernel page table root pointer to `*next` if valid.
     * Returns the current kernel page table root pointer.
     *
     * On platforms that only support a single root pointer, the split behaviour
     * is emulated.
     */
    HwMap HwKernelMap(sl::Opt<HwMap> next);

    /* Sets the current user-mode page table pointer to the `*next` field,
     * if its valid.
     */
    HwMap HwUserMap(sl::Opt<HwMap> next);

    /* Walks a page tree represented by `root`, for a given `vaddr`.
     * This function will walk the tree as far as it can go, and write details
     * about where it stopped to `result`.
     * 
     * Returns whether the walk was successful or not. If false, `result` is
     * untouched and may not be valid.
     */
    bool HwWalkMap(HwMap root, uintptr_t vaddr, MmuWalkResult& result, void* ptRef);

    /* Similar to `HwWalkMap()`, except this function assumes `ptRef` and
     * `result` have been filled in by a successful call to `HwWalkMap()`
     * earlier. This function will attempt to continue the page-table walk
     * as far as it'll go.
     *
     * Returns whether `result` and `ptRef` were updated. If false, the walk
     * could not be continued.
     */
    bool HwContinueWalk(HwMap root, uintptr_t vaddr, MmuWalkResult& result, void* ptRef);

    /* Manipulates an intermediate PTE (i.e. one that is not a final
     * level/translation).
     */
    bool HwIntermediatePte(HwPte* pte, sl::Opt<Paddr> next, bool valid);

    /* Returns whether a PTE is considerd valid/present. If `set` is valid,
     * the valid/present state of a PTE is updated to `*set`.
     */
    bool HwPteValid(HwPte* pte, sl::Opt<bool> set);

    /* Returns the flags set in a translatable PTE (i.e. the PTE must be at a
     * level where the MMU would complete address translation).
     * If `set` is valid, the PTE flags are updated to `*set`.
     */
    MmuFlags HwPteFlags(HwPte* pte, sl::Opt<MmuFlags> set);

    /* Returns the physical address mapped by a PTE. If `set` is valid,
     * the PTE's address is updated to `*set`.
     */
    Paddr HwPteAddr(HwPte* pte, sl::Opt<Paddr> set);

    /* Atomically copies a PTE from `src` to `dest`. PTEs should only be
     * modified as local copies (i.e. the PTE is not currently in an active
     * page table) as some MMUs may cache PTEs at *any* time.
     */
    void HwCopyPte(HwPte* dest, const HwPte* src);

    /* Returns the size (in bytes) of a page table at a given level.
     */
    size_t HwGetPageTableSize(size_t level);

    /* Checks if an address could be a valid user-space address. This function
     * only checks platform constraints, it does not consult the virtual
     * memory subsystem to see if anything is mapped or will be mapped at
     * this address.
     */
    bool HwIsCanonicalUserAddress(uintptr_t addr);

    /* Places the stack of return addresses into `store`.
     * `start` is the frame base pointer to begin at, or `0` if wanting to use
     * the current frame base pointer. `offset` allows a number of frames to
     * omitted before writing to `store`, useful for calling this function
     * multiple times in succession to get the full call chain (if store is
     * small).
     * 
     * Returns the number of return addresses placed in `store`. If equal to
     * store.Size(), it can be assumed the callstack continues further.
     */
    size_t GetCallstack(sl::Span<uintptr_t> store, uintptr_t start, 
        size_t offset = 0);

    /* Allows the hardware layer to output relevant info during a kernel panic,
     * like crucial system specifications. `maxWidth` references the max
     * line length output by `Print()`. `Print()` returns the length of the
     * formatted string, not including the null terminator.
     */
    void HwDumpPanicInfo(size_t maxWidth, 
        size_t (*Print)(const char* format, ...));

    /* Initializes any hardware state related to hardware debugging. This
     * runs on every cpu from within the debug event handler.
     * This function returns if debugging hardware initialized successfully.
     */
    bool HwInitDebugState();

    /* Attempts to enable a breakpoint at `addr`. The meaning of `kind` depends
     * on the type of breakpoint: for read or write breakpoints, its the length
     * in bytes to watch for (from `addr`). For exec breakpoints, its meaning
     * is implementation specific.
     * The `read`, `write`, `exec` and `hardware` arguments describe how the
     * breakpoint should be implemented. Both `read` and `write` can be
     * requested together, or individually. `exec` will never be set with 
     * `read` or `write`, and `hardware` can only be clear when `exec` is set.
     * As it would make no sense to have software breakpoints for read or write
     * operations.
     *
     * Returns whether the breakpoint was armed successfully.
     */
    bool HwEnableBreakpoint(HwBreakpoint& bp, uintptr_t addr, size_t kind,
        bool read, bool write, bool exec, bool hardware);

    /* Disarms a breakpoint at `addr`, `kind` should be the same value as
     * passed to `HwEnableBreakpoint()` for this address.
     * The breakpoint struct my be deallocated after this call, and should no
     * longer by referenced by hardware-layer code.
     * Returns whether disarming was successful. If it failed, the breakpoint
     * is still considered active.
     */
    bool HwDisableBreakpoint(HwBreakpoint& bp, uintptr_t addr, size_t kind);

    /*
     */
    size_t AccessRegister(TrapFrame& frame, size_t index, 
        sl::Span<uint8_t> buffer, bool get);

    /* Arms the local per-cpu timer to fire exactly once at a specified
     * time in the future. `expiry` is relative to same point in time as
     * the value returned by `HwReadTimestamp()`.
     */
    void HwSetAlarm(sl::TimePoint expiry);

    /* Returns the counter of a system-wide (or observed as such) monotonic
     * counter. The zero reference of the timer is assumed to be the (rough)
     * start time of the system.
     */
    sl::TimePoint HwReadTimestamp();

    /* Wastes time on the current cpu, at least as much as `duration`, but it
     * may be slightly more depending on the limits of `HwReadTimestamp()`.
     */
    SL_ALWAYS_INLINE
    void StallFor(sl::TimeCount duration)
    {
        auto start = HwReadTimestamp();
        auto end = duration.Rebase(start.Frequency).ticks + start.epoch;
        
        while (HwReadTimestamp().epoch < end)
            asm volatile("");
    }

    /* Sends an inter-processor interrupt to the cpu represented by `id`. The id
     * value is platform specific and can be obtained from a `CpuId` via a
     * call to `GetIpiId()` (see Core.hpp).
     */
    void HwSendIpi(void* id);
}

#ifdef __x86_64__
    #include <hardware/x86_64/Arch.hpp>
#else
#error "Compiling for unknown architecture."
#endif
