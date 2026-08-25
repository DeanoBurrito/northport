#pragma once

#include <Status.hpp>
#include <lib/Optional.hpp>
#include <lib/Span.hpp>
#include <lib/Flags.hpp>
#include <lib/Time.hpp>
#include <lib/Memory.hpp>
#include <lib/List.hpp>

namespace Npk
{
    /* Opaque type, represents a virtual address translation set.
     */
    struct HwMap;

    /* Address Space IDentifier: not required to have as many bits as the type
     * used below, the exact bit count cant be known until runtime on some
     * hardware. These are used as an optimization for local TLB ops, as only
     * entries with the matching asid are updated. The exception is when 
     * `AsidNone` (see below) is used, which indicates the operation should
     * affect tlb entries with no asid tag or other flags that would prevent
     * the operation from normally effecting it (x86 global flag).
     *
     * For systems without asid support, the meaning above still carries but 
     * all values that are not `AsidNone` carry the same meaning and touch
     * all regular tlb entries (those that would carry an asid if present).
     */
    using Asid = uint32_t;

    /* Special ASID, which must never be returned from the asid allocator.
     * This value indicates the tlb operation should effect all entries not
     * associated with a particular space, typically kernel entries.
     */
    constexpr Asid AsidNone = 0;

    /* Opaque type. Represents stored register state before a synchronous
     * exception/trap was fired.
     */
    struct TrapFrame;

    /* Opaque type. Represents arch-specific thread state, enough to save
     * and restore a kernel thread's state. Extended state used by userspace
     * is not required to be part of this structure.
     */
    struct HwThreadContext;

    /* Opaque type. Represents the blackbox that is userspace, including any
     * resources required to manage, enter, and exit it. There is no binding
     * between a kernel thread and a particular `HwUserContext`, the user
     * context is implicitly relevant inside of calls to `HwEnterUserContext()`.
     *
     * Note that a user context by itself is useless and trying to enter it will
     * abort. At least one `HwUserActivation` must exist for the user context
     * to be enterable. The activation contains the register image and execution
     * environment needed for the cpu to continue executing in userspace.
     *
     * TODO: better docs!
     */
    struct HwUserContext;

    /* Forward declaration, see Core.hpp.
     */
    struct ActivationList;

    /* Describes the major reason for an exit from a user mode context.
     */
    enum class HwUserExitType
    {
        /* The user context has been setup incorrectly or with bad values.
         * The meaning of the subtype field is arch-specific, but generally
         * higher values mean further progress to userspace. For specific
         * meanings the source code should be consulted.
         */
        InvalidEntryState = 0,

        /* User context indicated it has finished. The subtype field contains
         * an optional error code. The exact meaning of the error code is up
         * to the user code and it's source code should be consulted for
         * specific meanings.
         */
        Exit = 1,

        /* Enum value of `2` is currently unused and reserved for future use.
         */

        /* The user context is requesting to run a privileged function.
         * The subtype field indicates the function number.
         */
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

    enum class HwUserFeature
    {
        /* Valid values: `0`/`1`.
         * Set to emit a kernel log on the exit request of a user context.
         */
        LogExit,

        /* Valid values: `0`/`1`/`2`.
         * `0` to disable logging, `1` for logging syscall numbers (quick),
         * `2` for logging syscall numbers and arguments (sooooo sloooow).
         */
        LogSyscall,
    };

    /* Forward declaration, see Core.hpp for the full description.
     */
    struct ThreadContext;

    /* Forward declaration, see Debugger.hpp for the full description.
     */
    enum class DebugEventType;

    /* Determines additional permissions for an operation. Note that a read
     * permission is always implied. If a non-readable mapping is required, dont
     * map it!
     */
    enum class MmuPermission
    {
        Write,
        Fetch,
    };

    using MmuPermissions = sl::Flags<MmuPermission>;

    /* Caching modes for HwMap entries. These are used to describe the intent
     * of the mapping.
     */
    enum class MmuCacheMode
    {
        Default,
        Mmio,
        Framebuffer,
    };

    /*
     */
    struct HwBreakpoint
    {
        uint8_t backupLength;
        uint8_t bind;
        uint8_t backup[sizeof(uintptr_t)];
    };

    /* Determines the operation(s) to perform on a hardware cache.
     */
    enum class HwCacheOp
    {
        /* Cleans cache lines completely, ensuring cached data is visible to
         * other cpus and devices in the system.
         */
        Clean,

        /* Weaker form of cleaning: only ensures changed data is visible to
         * other cpus in the system, not necessarily devices. On many
         * architectures this is implemented the same way as `Clean`, but some
         * allow this operation to have a lesser performance cost.
         */
        CleanForCpus,

        /* Marks cache lines as invalid, meaning data should be fetched from
         * main memory when next needed. Any changes made to data in the cache
         * are lost.
         */
        Invalidate,
    };

    using HwCacheOps = sl::Flags<HwCacheOp>;

    /* Determines the type(s) of cache to operate on.
     */
    enum class HwCacheType
    {
        /* Instruction cache.
         */
        ICache,

        /* Data cache.
         */
        DCache,
    };

    using HwCacheTypes = sl::Flags<HwCacheType>;

    enum class HwRegType
    {
        Common = 0,
        General = 1,
        FloatingPoint = 2,
        Vector = 3,
        System = 4,
    };

    constexpr size_t HwRegShift = 16;

    enum class HwReg
    {
        /*
         */
        CommonBase = (size_t)HwRegType::Common << HwRegShift,
        ProgramCounter,
        StackPointer,
        FramePointer,
        Flags,

        /*
         */
        GeneralBase = (size_t)HwRegType::General << HwRegShift,

        /*
         */
        FloatingPointBase = (size_t)HwRegType::FloatingPoint << HwRegShift,

        /*
         */
        VectorBase = (size_t)HwRegType::Vector << HwRegShift,

        /*
         */
        SystemBase = (size_t)HwRegType::System << HwRegShift,
    };

    /* Represents a region of virtual address space that the kernel can use
     * as a direct map. Meaning a page at `paddr` can be accessed as
     * `vaddr = paddr - seg->base + seg->offset`. All field values of a segment
     * must be page-aligned.
     */
    struct HwDirectMapSegment
    {
        /* Virtual base address of the segment: where its translation window
         * can be found from the kernel's perspective. This is bounded by
         * `length.
         */
        uintptr_t virtBase;

        /* Physical base address of the segment: where it begins translating
         * addresses from. This is bounded by `length`.
         */
        Paddr physBase;

        /* Determines the length of the segment.
         */
        size_t length;
    };

    struct HwAddressRange
    {
        uintptr_t base;
        uintptr_t top;
    };

    enum class PageVmFlag
    {
        Busy,
        Clean,
    };

    using PageVmFlags = sl::Flags<PageVmFlag, uint8_t>;

    constexpr uintptr_t PageVmOwnerTypeMask = 0b11;

    enum class PageVmOwnerType
    {
        Source,
        Anon,
    };

    struct PageInfo
    {
        sl::FwdListHook mmList;
        union
        {
            struct
            {
                size_t count;
            } pm;

            sl::FwdListHook vmoList;
            struct
            {
                char placeholder[sizeof(vmoList)];
                uintptr_t owner; //NOTE: dont use directly, see helpers below.
                uint32_t offset; //of page in object, counts in pages.
                uint32_t pins : 24;
                PageVmFlags flags;
            } vm;
        };
    };
    static_assert(sizeof(PageInfo) <= (sizeof(void*) * 4));

    using PageList = sl::FwdList<PageInfo, &PageInfo::mmList>;

    /* Calls `func` passing `a`/`b`/`c` as params to it, optionally placing
     * the return value of `func` into `*r` if non-null. If a synchronous
     * exception (i.e. one occurring due to an instruction `func` executed)
     * this function aborts further execution of `func` and returns true
     * (an exception occurred), the value of `*r` is left unchanged if non-null. 
     * If `func` returned normally, this function returns false.
     *
     * This function can be nested: an exception will causes only the innermost
     * call to this function to return failure.
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

    /* Represents a valid but empty trap frame struct. This is intended for use
     * with APIs that require a valid trap frame when the caller cannot provide
     * one.
     */
    TrapFrame* IdentityTrapFrame();

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

    /* Attempts to create a new user context, on success a pointer to the
     * context is placed in `*context`.
     */
    NpkStatus HwCreateUserContext(HwUserContext** context);

    /* Attempts to destroy a user context, releasing any associated resources.
     * Note that the context must have no activations currently attached to it,
     * and must be active/entered on any cpu.
     */
    NpkStatus HwDestroyUserContext(HwUserContext* context);

    /* This function places the current cpu in an unprivileged (user) mode and
     * transfers control to the top-most activation linked to `context`. This
     * function will eventually return when the unprivileged code causes an exit
     * event that requires action from the kernel, the event details are
     * returned from this function. Some events may not cause this function to
     * directly return, and may route directly to other subsystems first. For 
     * example a page fault is routed to the virtual memory subsystem and only
     * if it is found to be a bad page fault does the user context exit and this
     * function return, otherwise it is handled transparently.
     *
     * Other exit events include system calls and synchronous cpu exceptions,
     * asynchronous exceptions and device interrupts are not related to the
     * context so they do cause an exit from usermode.
     */
    HwUserExitInfo HwEnterUserContext(HwUserContext& context);

    /* Allows for reading, and more importantly - setting, feature flags/values
     * for a user context. Each feature has different semantics, most will be
     * binary flags or limited-range integers. See the comments of each value
     * in the `HwUserFeature` enum for details of each feature.
     * The return value is whether the get/set operation was successful.
     */
    bool HwGetSetUserContextFeature(HwUserContext& context, 
        HwUserFeature feat, size_t* value, bool set);

    /* Hook function called when creating a new activation. This function is
     * called at passive IPL and may block if needed, the value in `*outPrivate`
     * after returning is placed into the activation's `hwPrivate` field.
     * This is intended for storing hardware specific state required by an
     * activation, such as extended or system registers.
     */
    NpkStatus HwCreateUserActivation(void** outPrivate);

    /* Companion function to `HwCreateUserActivation()`, called to clean up
     * resources previously allocated by a call to that function. These
     * functions are always called in matched pairs, there is only one destroy
     * call for one create call.
     */
    NpkStatus HwDestroyUserActivation(void* privateData);

    /* Returns a reference to the activations list for the specified context.
     */
    ActivationList& HwGetUserContextActivations(HwUserContext& context);

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

    /* Enables interrupts, returns whether interrupts were previously enabled.
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
     * event args struct (or nullptr, if the event defines none).
     * Event arg structs are named after their associated event: an event
     * of type `Breakpoint` would pass the address of a `BreakpointEventArg`
     * struct as `data`.
     */
    SL_ALWAYS_INLINE
    NpkStatus HwCallDebugger(DebugEventType type, void* data);

    /* Set the physical address mapped by a temporary map slot at `index`.
     * Returns nullptr on error, or the virtual address `paddr` can be found at.
     */
    void* HwSetTempMapSlot(size_t index, Paddr paddr);

    /* Returns the valid range of userspace addresses based on platform limits.
     * Note this does not check with the VM subsystem to determine if the
     * address is valid, just that it makes sense.
     */
    HwAddressRange HwGetUserAddressRange();

    /* Returns any valid address ranges that might be used by the kernel, based
     * on platform limitations. This is similar to the userspace counterpart
     * (`HwGetUserAddressRange()`) in that it does not check with the VM 
     * subsystem.
     */
    sl::Span<HwAddressRange> HwGetKernelAddressRanges();

    /* Returns the available direct map segments. Note that these values are
     * not allowed to change after the first call of this function: the 
     * direct map segments are immutable once setup.
     */
    sl::Span<const HwDirectMapSegment> HwGetDirectMapSegments();

    /*
     */
    NpkStatus HwMapCreate(HwMap** map);

    /*
     */
    void HwMapDestroy(HwMap* map);

    /*
     */
    HwMap* HwKernelMap();

    /*
     */
    void HwMapActivate(HwMap* map);

    /*
     */
    NpkStatus HwMapAdd(HwMap* map, uintptr_t vaddr, Paddr paddr, 
        MmuPermissions perms, MmuCacheMode cacheMode, bool wired);

    /*
     */
    NpkStatus HwMapRemove(HwMap* map, uintptr_t vaddr, size_t count);

    /*
     */
    NpkStatus HwMapProtect(HwMap* map, uintptr_t vaddr, size_t count, 
        MmuPermissions perms);

    /*
     */
    NpkStatus HwMapExtract(Paddr* outPaddr, MmuPermissions* outFlags, 
        MmuCacheMode* outMode, HwMap* map, uintptr_t vaddr);

    /*
     */
    void HwMapSetWired(HwMap* map, uintptr_t vaddr, bool wired);

    /*
     */
    bool HwMapGetAccessed(HwMap* map, uintptr_t vaddr);

    /*
     */
    bool HwMapGetDirty(HwMap* map, uintptr_t vaddr);

    /*
     */
    bool HwMapClearAccessed(HwMap* map, uintptr_t vaddr);

    /*
     */
    bool HwMapClearDirty(HwMap* map, uintptr_t vaddr);

    /*
     */
    bool HwHandleMinorFaultOnMap(HwMap* map, uintptr_t vaddr, bool write);

    /* Changes to a `HwMap` may not take effect immediately, and on remote cpus
     * these changes may take even longer to take effect. If an existing mapping
     * has been modified there may be stale caches of it on some cpus.
     *
     * This function is a synchronization point for a `HwMap`: it causes all
     * cpus in the same domain to ensure they see the latest version (when this
     * function was called) of what the map contains. Note that if permissions
     * are added to a mapping, or the mapping is new, the page fault handler
     * or hardware will take care of that. A call to this function is required
     * when a mapping's permissions are decreased or the mapping is removed.
     *
     * The `sync` argument determines if this function should wait until all
     * relevant cpus have actioned and acknowledged the update. If clear this
     * function returns after only synchronizing the local cpu's view of the
     * map. Any pages in `freeAfter` are freed after all cpus have acknowledged
     * the map update.
     *
     * This function must be called at passive IPL if `sync` is set, otherwise
     * it may be called at DPC IPL.
     */
    void HwMapUpdate(HwMap* map, bool sync, PageList& freeAfter);

    /* Returns if the current system has hardware support for invalidating 
     * remote TLBs. If true, `HwInvalidateTlbs()` and `HwSyncTlbs()` will be
     * used. If false, a software based tlb sync mechanism will be used and 
     * those functions won't be called.
     */
    bool HwHasBroadcastInvalidate();

    /* Asks all cpus aware of `map` to invalidate `vaddr` -> `vaddr + len`.
     * This function returns after starting the operation but does not wait for
     * it to finish on remote cpus, meaning the system still has an inconsistent
     * view of memory. See `HwSyncTlbs()` below for that behaviour.
     * Only called when `HwHasBroadcastInvalidate()` returns true.
     */
    void HwInvalidateTlbs(HwMap* map, uintptr_t vaddr, size_t length);

    /* Holds the current cpu (not waiting or otherwise blocking) until all
     * prior calls to `HwInvalidateTlbs()` on this cpu have been confirmed
     * actioned by all targetted cpus.
     * Only called when `HwHasBroadcastInvalidate()` returns true.
     */
    void HwSyncTlbs();

    /* Returns the count where it's cheaper to flush the whole tlb rather than
     * individual entries.
     */
    size_t HwGetTlbFlushThreshold();

    /* Flush the local TLB for virtual addresses in `base` -> `base + length`,
     * for entries tagged with `asid`.
     */
    void HwFlushTlb(Asid asid, uintptr_t base, size_t length);

    /* Flush local TLB entries tagged with `asid`, regardless of address.
     */
    void HwFlushTlbAll(Asid asid);

    /* Flush caches relevant to the local cpu for addresses in the range
     * indicated by `base` and `length`. The `types` field determines which
     * caches get flushed, and `ops` determines what happens with each selected
     * cache.
     */
    void HwFlushCache(uintptr_t base, size_t length, HwCacheOps ops, 
        HwCacheTypes types);

    /* Similar to `HwFlushCache()` but operates on entire caches, regardless of
     * associated addresses.
     */
    void HwFlushCacheAll(HwCacheOps ops, HwCacheTypes types);

    /* Returns the largest cache line in bytes of any cpu core usable by the
     * current kernel.
     */
    size_t HwGetCacheLineSize();

    /* Returns the worst (largest) cache line size expected on a the target
     * system. This must be known at compile time so it can be used for aligning
     * and padding structs.
     */
    constexpr size_t HwGetStaticCacheLineSize();

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

    /* Returns the smallest number of hardware breakpoints supported by any
     * cpu core in the system. Hardware breakpoints are mirrored between cpus,
     * so only the smallest common value is usable by the debugger.
     * This function will only run after all cores have run `HwInitDebugState()`
     * successfully.
     */
    size_t HwGetBreakpointCount();

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

    /* Allows generic code to access hardware register values from a trap frame
     * or their active values (if not represented by a trap frame, which only
     * backs up enough state for the kernel to run).
     * The `write` flag indicates the type of operation: if set, the function
     * attempts to write the contents of `buffer` to the register. If cleared,
     * register contents are written to `buffer`, space permitting. If `buffer`
     * is not large enough to write into, the register value is truncated. If 
     * `buffer` does not contain enough data to fill the register the unfilled
     * parts of the register have undefined contents - the exact value depends
     * on what hardware allows for. In most cases these bits should contain
     * their original value but this shouldn't be relied upon.
     * If `usedLen` is non-null, the function writes the number of bytes read/
     * written to `*usedLen`.
     */
    NpkStatus HwAccessRegister(TrapFrame& frame, HwReg reg, size_t* usedLen,
        sl::Span<uint8_t> buffer, bool write);

    /* Returns the number of **bytes** required to hold the value of the
     * specified register.
     */
    size_t HwGetRegisterWidth(HwReg reg);

    /* Returns the count of available registers of a particular class.
     */
    size_t HwGetRegisterCount(HwRegType type);

    /* Enables/Disables single stepping for the instruction stream represented
     * by `frame`.
     */
    void HwSetSingleStep(TrapFrame& frame, bool on);

    /* Returns whether single stepping is enabled for the instruction stream
     * represented by `frame`.
     */
    bool HwGetSingleStep(TrapFrame& frame);

    /* Arms the local per-cpu hardware timer and sets the local deadline for 
     * `expiry`. The expiry time is relative to the same point as
     * `HwReadTimestamp()` (often system uptime). The implementation must handle
     * `expiry` being set to a current or past time and should fire the alarm
     * as soon as possible. The alarm is allowed to fire early as it is only
     * advisory.
     * Once the timer is armed it is expected to generate exactly one timer
     * interrupt (resulting in a call to `DispatchAlarm()`) unless 
     * `HwClearAlarm()` is successfully called before expiry. Further calls to
     * this function while the timer is armed update it's expiry time but leave
     * it logically armed.
     */
    void HwSetAlarm(sl::TimePoint expiry);

    /* Cancels the pending alarm for the per-cpu timer.
     */
    void HwClearAlarm();

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

    /* Causes an interrupt to be received on the cpu with software assigned id
     * `who`. This will appear to portable kernel code as a call to
     * `DispatchIpi()` on the target cpu.
     */
    void HwSendIpi(CpuId who);
}

#ifdef __x86_64__
    #include <hardware/x86_64/Arch.hpp>
#else
#error "Compiling for unknown architecture."
#endif
