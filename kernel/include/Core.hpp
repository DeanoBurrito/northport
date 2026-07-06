#pragma once

#include <Hardware.hpp>
#include <lib/List.hpp>
#include <lib/LruCache.hpp>
#include <lib/Queue.hpp>
#include <lib/Locks.hpp>
#include <lib/Efi.hpp>

extern "C" char KERNEL_CPULOCALS_BEGIN[];
extern "C" char KERNEL_NODELOCALS_BEGIN[];

namespace sl
{
    struct Sdt;
}

namespace Npk
{
    using HeapTag = uint32_t;

    constexpr uint8_t MinPriority = 0;
    constexpr uint8_t MaxPriority = 255;

    /* Fixed priority of idle-class threads
     */
    constexpr uint8_t IdlePriority = MinPriority;

    /* Minimum priority for realtime-class threads
     */
    constexpr uint8_t MinRtPriority = MaxPriority / 2;

    /* Maximum priority for realtime-class threads
     */
    constexpr uint8_t MaxRtPriority = MaxPriority;

    /* Minimum priority for timeshared-class (aka general purpose) threads
     */
    constexpr uint8_t MinTsPriority = IdlePriority + 1;

    /* Maximum priority for timeshared-class (aka general purpose) threads
     */
    constexpr uint8_t MaxTsPriority = MinRtPriority - 1;

    /* Lowest possible niceness value, in absolute form. This is interpreted
     * as relative to `BaseNiceness`.
     */
    constexpr uint8_t MinNiceness = 0;

    /* Default niceness value, this value is also treated as the midpoint for
     * nice values. Anything below this is considered a negative value.
     * Niceness values are an additional input when computing thread priorities,
     * the intent is that setting a thread's priority is a privileged operation
     * while adjusting niceness can be less privileged.
     * Niceness values are clamped by `MinNiceness` and `MaxNiceness`.
     */
    constexpr uint8_t BaseNiceness = 20;

    /* Highest possible niceness value, in absolute form. This is interpreted
     * as relative to `BaseNiceness`.
     */
    constexpr uint8_t MaxNiceness = 39;

    /* Spinlock which disables interrupts on the local cpu while held.
     */
    class IntrSpinLock
    {
    private:
        sl::SpinLock lock;
        bool prevIntrs;

    public:
        constexpr IntrSpinLock() : lock {}, prevIntrs(false)
        {}

        inline void Lock()
        {
            prevIntrs = IntrsOff();
            lock.Lock();
        }

        inline bool TryLock()
        {
            const bool restoreIntrs = IntrsOff();
            const bool success = lock.TryLock();

            if (success)
                prevIntrs = restoreIntrs;
            else if (restoreIntrs)
                IntrsOn();

            return success;
        }

        inline void Unlock()
        {
            lock.Unlock();
            if (prevIntrs)
                IntrsOn();
        }
    };

    /* Interrupt Priority Level. Higher IPLs will mask and preempt lower IPLs.
     * This can be used to prevent behaviours which occur at specific IPLs,
     * or ensure mutual exclusion within a single cpu core (locks are still
     * required when multiple cores can be involved).
     *
     * E.g. thread preemption only occurs when the current IPL is passive,
     * if kernel wants to prevent being preempted but leave interrupts enabled,
     * it can raise the IPL > Passive.
     */
    enum class Ipl : uint8_t
    {
        Passive,
        Dpc,
        Interrupt,
    };

    /* Spinlock which can only be acquired when the `min <= current IPL <= max`.
     * If unspecified, `min` is Ipl::Passive meaning the template param defines
     * the maximum IPL the lock can be taken at. If required, holding this lock
     * raises the local IPL to `max` level.
     */
    template<Ipl max, Ipl min = Ipl::Passive>
    class IplSpinLock
    {
    private:
        sl::SpinLock lock;
        Ipl prevIpl;

    public:
        constexpr IplSpinLock() : lock {}, prevIpl {}
        {}

        inline void Lock();
        inline bool TryLock();
        inline void Unlock();
    };

    enum class ConfigRootType
    {
        Rsdp,
        Fdt,
        BootInfo,
    };

    template<typename T>
    class CpuLocal
    {
    private:
        alignas(T) char store[sizeof(T)];

    public:
        constexpr CpuLocal() = default;

        T* Get()
        {
            const uintptr_t base = MyCpuLocals();
            const uintptr_t offset = reinterpret_cast<uintptr_t>(this) -
                reinterpret_cast<uintptr_t>(KERNEL_CPULOCALS_BEGIN);

            return reinterpret_cast<T*>(base + offset);
        }

        T* operator&()
        {
             return Get();
        }

        T* operator->()
        {
            return Get();
        }

        T& operator*()
        {
            return *Get();
        }

        void operator=(const T& latest)
        {
            *Get() = latest;
        }
    };

    namespace Private
    {
        uintptr_t MyNodeLocals();
    }

    template<typename T>
    class NodeLocal
    {
    private:
        alignas(T) char store[sizeof(T)];

    public:
        constexpr NodeLocal() = default;

        T* Get()
        {
            const uintptr_t base = Private::MyNodeLocals();
            const uintptr_t offset = reinterpret_cast<uintptr_t>(this) -
                reinterpret_cast<uintptr_t>(KERNEL_NODELOCALS_BEGIN);

            return reinterpret_cast<T*>(base + offset);
        }

        T* operator&()
        {
             return Get();
        }

        T* operator->()
        {
            return Get();
        }

        T& operator*()
        {
            return *Get();
        }

        void operator=(const T& latest)
        {
            *Get() = latest;
        }
    };

    namespace Private
    {
        bool PmaCacheSetEntry(size_t slot, void** curVaddr, Paddr curPaddr, 
            Paddr nextPaddr);
    };

    using PageAccessCache = sl::LruCache<Paddr, void*, 
        Private::PmaCacheSetEntry>;

    struct PageAccessRef
    {
    private:
        PageAccessCache::CacheRef slot;

    public:
        void* vaddr;
        Paddr paddr;

        PageAccessRef() 
            : slot {}
        {}

        PageAccessRef(PageAccessCache::CacheRef slot)
            : slot(sl::Move(slot))
        {}
        
        bool Valid()
        {
            return vaddr != nullptr;
        }

    };

    struct Dpc;

    using DpcEntry = void (*)(Dpc* self, void* arg);

    struct Dpc
    {
        sl::FwdListHook hook;
        DpcEntry function;
        void* arg;
        sl::Atomic<bool> complete;
    };

    using DpcQueue = sl::FwdList<Dpc, &Dpc::hook>;

    struct WorkItem;
    struct RemoteCpuStatus;

    using WorkItemEntry = void (*)(WorkItem* self, void* arg);

    enum class WorkItemState
    {
        Invalid,
        Idle,
        Pending,
        Executing,
        PendingCancel,
    };

    struct WorkItem
    {
        sl::QueueMpScHook hook;
        WorkItemEntry function;
        void* arg;
        sl::Atomic<WorkItemState> state;
        RemoteCpuStatus* queue;
    };

    using WorkItemQueue = sl::QueueMpSc<WorkItem, &WorkItem::hook>;

    enum class CycleAccount
    {
        User,
        Kernel,
        KernelInterrupt,
        Driver,
        DriverInterrupt,
    };

    struct Waitable;
    struct ClockQueue;

    struct ClockEvent
    {
        Dpc* dpc;
        Waitable* waitable;
        sl::ListHook hook;
        sl::TimePoint expiry;
        sl::Atomic<ClockQueue*> queue;
    };

    using ClockList = sl::List<ClockEvent, &ClockEvent::hook>;

    struct ClockQueue
    {
        IplSpinLock<Ipl::Dpc> lock;
        ClockList events;
    };

    enum class WaitStage : uint8_t
    {
        Preparing,
        Blocked,
        Satisfied,
        Timedout,
        Cancelled,
        Reset,
    };

    enum class WaitableType : uint8_t
    {
        Condition,
        Timer,
        Mutex,
        SxMutex,
    };

    struct WaitEntry;
    struct ThreadContext;
    struct Waitable;

    struct WaitEntry
    {
        sl::ListHook waitableQueueHook;

        ThreadContext* thread;
        Waitable* waitable;
        bool satisfied;
        bool isExclusive;
        bool inList;
    };

    using WaitEntryList = sl::List<WaitEntry, &WaitEntry::waitableQueueHook>;

    /* Represents an object that threads can block on, and resume execution
     * when some condition is met. The exact behaviour of a waitable depends
     * on its `type` and `ticket` count fields. A waitable must be reset
     * via a call to `ResetWaitable()` before it can be used.
     *
     * `Condition` types act like reference count. Resetting them sets their
     * ticket count to the requested value, signalling decrements the ticket
     * count by one until it reaches zero. Once the ticket count is zero, all
     * waits (current and future) are satisfied immediately until the object
     * is reset. While it has a non-zero ticket count, any waits on this object
     * will block.
     *
     * `Timer` types block all waiters until the integrated `ClockEvent`
     * expires, after which it will satisfy all current and future waiters until
     * reset again. This type of event is signalled by the clock subsystem,
     * there is no mechanism to signal it manually.
     *
     * `Mutex` types are intended for use as blocking locks. The ticket count
     * represents the number of locks available, typically this will be reset
     * to just one, for a true mutex. Threads will block on mutexes with a 
     * ticket count of 0, and will only be satisfied when they can successfully
     * decrement the ticket count without it going below zero.
     *
     * `SxMutex` types are similar to `Mutex` types but can be held shared or
     * exclusive. An SxMutex that is held shared can be acquired shared by
     * other threads, and if the SxMutex is held exclusive no other thread
     * can acquire it. An SxMutex also has a batching mechanism where if many 
     * threads attempt to acquire an SxMutex shared but cannot (and therefore
     * block) they will all be woken at the same time, provided there are enough
     * tickets available.
     */
    struct Waitable
    {
        WaitableType type;
        IplSpinLock<Ipl::Dpc> listLock;
        sl::Atomic<bool> pending;

        sl::Atomic<size_t> tickets;
        WaitEntryList waitersList;
        sl::QueueMpScHook mpscHook;
        sl::FwdListHook ownerListHook;

        union
        {
            ThreadContext* owner;
            ClockEvent clockEvent;
        };
    };

    using Condition = Waitable;
    using Timer = Waitable;
    using Mutex = Waitable;
    using SxMutex = Waitable;

    using WaitableMpScQueue = sl::QueueMpSc<Waitable, &Waitable::mpscHook>;
    using WaitableOwnerList = sl::FwdList<Waitable, &Waitable::ownerListHook>;
    
    using MailFunction = void (*)(void* arg);

    struct SmpMail
    {
        sl::QueueMpScHook mpscHook;

        MailFunction function;
        void* arg;
        Waitable* onComplete;
    };

    using MailQueue = sl::QueueMpSc<SmpMail, &SmpMail::mpscHook>;

    struct FlushRequest
    {
        sl::QueueMpScHook hook;

        uintptr_t base;
        size_t length;
        sl::Atomic<size_t> acknowledgements;
    };

    using FlushRequestQueue = sl::QueueMpSc<FlushRequest, &FlushRequest::hook>;

    struct LocalScheduler;

    struct RemoteCpuStatus
    {
        sl::Atomic<sl::TimePoint> lastIpi;
        LocalScheduler* scheduler;
        WorkItemQueue workItems;
        Condition workItemsPending;
        TrapFrame* lastIntrFrame;
        sl::Atomic<uint8_t> performanceCapacity;
        sl::Atomic<uint8_t> efficiencyClass;
    };

    struct alignas(64) SmpControl
    {
        void* ipiId;
        MailQueue mail;
        FlushRequestQueue shootdowns;
        RemoteCpuStatus status;
    };

    enum class LogLevel
    {
        Error,
        Warning,
        Info,
        Verbose,
        Trace,
        Debug,
    };

    struct LogSinkMessage
    {
        sl::StringSpan text;
        sl::StringSpan who;
        sl::TimePoint when;
        LogLevel level;
        CpuId cpu;
    };

    struct LogSink
    {
        sl::ListHook listHook;

        void (*Reset)();
        void (*Write)(LogSinkMessage msg);
        void (*BeginPanic)();
    };

    using LogSinkList = sl::List<LogSink, &LogSink::listHook>;

    enum class PageVmFlag
    {
        Busy,
        Clean,
    };

    using PageVmFlags = sl::Flags<PageVmFlag, uint32_t>;

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
                PageVmFlags flags;
                uint16_t offset;
                uint16_t refcount;
            } vm;

            struct
            {
                uint16_t validPtes;
            } mmu;
        };
    };
    static_assert(sizeof(PageInfo) <= (sizeof(void*) * 4));

    using PageList = sl::FwdList<PageInfo, &PageInfo::mmList>;

    struct VmSpace;

    struct SystemDomain
    {
        Paddr physOffset;
        PageInfo* pfndb;

        CpuId smpBase;
        sl::Span<SmpControl> smpControls;

        uintptr_t pmaBase;
        HwMap kernelMap;
        VmSpace* kernelSpace;
        Paddr zeroPage;
        
        struct
        {
            IntrSpinLock lock;
            size_t pageCount;
            PageList free;
            PageList zeroed;
        } freeLists;

        struct 
        {
            Mutex lock;
            PageList active;
            PageList dirty;
            PageList standby;
        } liveLists;

        //TODO: io + device linkage
    };

    /* Possible states of existence for a thread.
     */
    enum class ThreadState : uint8_t
    {
        /* Thread has finished executing and its resources are pending cleanup.
         * The only way for a thread to move away from this state is by calling
         * `ResetThread()` and then `PrepareThread()`.
         * Freshly created thread blocks also have this state.
         */
        Dead,

        /* Thread control block is in a valid state, but not pending execution
         * anywhere. `EnqueueThread()` should be called to schedule the thread.
         */
        Standby,

        /* Thread is queued for execution on a core, either by being in its
         * run queues or being placed in the `NextThread` slot. The
         * `scheduling.affinity` field indicates which core the thread is
         * readied on.
         */
        Ready,

        /* Thread is currently running the core indicated by
         * `scheduling.affinity`. It may move to any of the other states.
         */
        Executing,

        /* Internal state: indicates a thread is currently executing but will
         * transition to Waiting instead of Ready when next scheduled away from.
         */
        WaitPending,

        /* Thread is currently waiting on an event/waitable. When the wait is
         * completed (by satisfaction or by timing out) the thread will wake
         * and be placed in a core's run queues and move to the `Ready` state.
         */
        Waiting,
    };

    enum class PowerHint : uint8_t
    {
        Default,
        Efficient,
        Performance,
    };

    struct SchedulerStats
    {
        sl::Atomic<uint64_t> version;
        uint64_t contextSwitchCount;
        uint64_t quantumEndCount;
        uint64_t preemptCount;
        uint64_t yieldCount;
        uint64_t stealsAttempted;
        uint64_t stealsSuccessful;
        uint64_t migrationsTo;
        uint64_t migrationsFrom;
        uint64_t idleCount;
    };

    struct ThreadStats
    {
        sl::Atomic<uint64_t> version;
        uint64_t contextSwitchCount;
        uint64_t quantumEnds;
        uint64_t migrationCount;
        uint64_t waitPercentage;
        uint64_t userNanos;
        uint64_t kernelNanos;
    };

    struct ThreadContext
    {
        ThreadStats accounting;

        struct
        {
            IplSpinLock<Ipl::Dpc> lock;
            HwThreadContext* context;

            CpuId affinity;
            sl::TimePoint sleepBegin;
            uint32_t sleepTime;
            uint32_t runTime;
            uint8_t basePriority;
            uint8_t boostPriority;
            bool isPinned;
            ThreadState state;
            bool isInteractive;
            uint8_t niceness;
            PowerHint powerHint;
            bool agingBoost; //TODO: implement
            bool inRunQueue;
            sl::Span<WaitEntry> waitingOn;
            WaitableOwnerList heldLocks;
        } scheduling;
        sl::ListHook queueHook; //NOTE: protected by queuesLock

        struct
        {
            sl::Atomic<WaitStage> stage;
            Dpc* wakeDpc;
            IplSpinLock<Ipl::Dpc> lock;
            sl::StringSpan reason;
        } waiting;
    };

    using ThreadQueue = sl::List<ThreadContext, &ThreadContext::queueHook>;

    extern SystemDomain sysDomain0;

    /* Can be called from any IPL, appends a message to the kernel log queue.
     * There are no lifetime guarantees for a log message once in the queue,
     * but a written message will eventually be consumed by any active log
     * sinks. The `msg` argument may contain printf style format specifiers.
     */
    SL_PRINTF_FUNC(1, 3)
    void Log(const char* msg, LogLevel level, ...);

    /* Similar to `Log()` except the function brings down the entire system,
     * captures and displays some debug info alongside the panic message.
     * If `frame` is non-null, the stored program state will also be included
     * in the debug data and can aid later diagnostics. Like `Log()`, `msg` may
     * contain printf style format specifiers.
     * This function can be called from any IPL and never returns.
     */
    [[noreturn]]
    void Panic(sl::StringSpan message, TrapFrame* frame, ...);

    void AddLogSink(LogSink& sink);

    /* Detaches a previously registered log sink. Once removed the sink receives
     * no further messages and can be safely torn down.
     * Safe to call at any IPL.
     */
    void RemoveLogSink(LogSink& sink);

    /* Enum value to string function for `enum LogLevel`.
     */
    sl::StringSpan LogLevelStr(LogLevel which);

    void AssertIpl(Ipl target);

    /* Returns the current IPL for the local CPU. Note the return value is
     * only meaningful if preemption is disabled, otherwise the value may be
     * stale if a thread is migrated between calling this function and using
     * the value.
     */
    Ipl CurrentIpl();

    /* Strictly raises the local cpu's IPL to `target`, masking any activity
     * that runs at or below the previous level. Returns the previous IPL so it
     * can be later restored via a call to `LowerIpl()`.
     */
    Ipl RaiseIpl(Ipl target);

    /* Strict lowers the local cpu's IPL to `target`, unmasking any activity
     * that held off at higher levels. Any work at the newly unmasked levels is
     * performed before this function returns.
     */
    void LowerIpl(Ipl target);

    /* Acquires the lock, must be called at IPL <= max IPL of lock.
     */
    template<Ipl max, Ipl min>
    inline void IplSpinLock<max, min>::Lock()
    {
        const auto lastIpl = CurrentIpl();
        if (lastIpl > max || min > lastIpl)
            Panic("Bad IPL when acquiring IplSpinLock", nullptr);

        if (lastIpl < max)
            RaiseIpl(max);

        lock.Lock();
        prevIpl = lastIpl;
    }

    /* TryAcquire version of `IplSpinLock::Lock()`. Attempts to acquire the lock
     * once before returning. Returns whether the lock was successfully acquired
     * or not.
     */
    template<Ipl max, Ipl min>
    inline bool IplSpinLock<max, min>::TryLock()
    {
        const auto lastIpl = CurrentIpl();
        if (lastIpl > max || min > lastIpl)
            Panic("Bad IPL when trying to acquire IplSpinLock", nullptr);

        if (lastIpl < max)
            RaiseIpl(max);
        const bool success = lock.TryLock();

        prevIpl = lastIpl;
        if (!success)
            LowerIpl(prevIpl);

        return success;
    }

    /* Releases the lock and restores the local IPL to the level it was at when
     * the lock was acquired.
     */
    template<Ipl max, Ipl min>
    inline void IplSpinLock<max, min>::Unlock()
    {
        const auto lastIpl = prevIpl;
        lock.Unlock();

        if (lastIpl < max)
            LowerIpl(lastIpl);
    }

    /* Resets and initializes a DPC instance struct.
     */
    NpkStatus ResetDpc(Dpc* dpc, DpcEntry func, void* arg, bool force);

    /* Queues a DPC for execution on the current cpu. This function can be
     * called at any level: if run from below IPL::DPC, it will raise the local
     * ipl and execute the DPC immediately.
     */
    void QueueDpc(Dpc* dpc);

    /* Must be called at passive IPL: spins until the target DPC has finished
     * execution.
     */
    void SpinUntilDpcCompleted(Dpc* dpc);

    /* Resets a work item struct to it's initial (usable) state, this function
     * can be called on an idle struct or one that has been zero-initialized.
     * This function has no IPL requirement.
     */
    NpkStatus ResetWorkItem(WorkItem* item, WorkItemEntry func, void* arg);

    /* Places a work item in a queue, optionally on a specific cpu. This
     * function can be called on idle or executing work items (a work item can
     * re-queue itself while running). Work item functions are allowed to take
     * mutexes and otherwise block, but they are not recommended for waiting on
     * long-running tasks. This function has no IPL requirement.
     */
    NpkStatus QueueWorkItem(WorkItem* item, sl::Opt<CpuId> who);

    /* This functions only returns when `item` has been marked as idle, meaning
     * it's not queued anywhere and is not currently executing.
     * The `spin` flag determines if this function busy waits on the work item,
     * or uses a blocking wait. If called with `spin = false`, it must be done
     * so at passive IPL, otherwise there is no IPL requirement.
     */
    NpkStatus WaitUntilWorkItemComplete(WorkItem* item, bool spin);

    /* Requests cancellation of a work item: if the item is queued or pending
     * execution the item is skipped, if the item is executing already it is
     * allowed to complete. The `wait` and `spin` arguments control when this
     * function returns: `wait` determines if the function should ensure the
     * work item has finished before returning, `spin` determines how the wait
     * happens (`spin = true` spins, `spin = false` uses blocking).
     * There is no IPL requirement for this function unless 
     * `wait = true, spin = false` is passed, in which can the caller must be
     * at passive IPL.
     */
    NpkStatus CancelWorkItem(WorkItem* item, bool wait, bool spin);

    /* Get access to some cpu-local variables of another cpu. This can be an
     * expensive operation, best used sparingly.
     */
    RemoteCpuStatus* RemoteStatus(CpuId who);

    /* Queue a function to run on a remote cpu, mail is processed at interrupt
     * IPL and can be a heavy primitive to use. For less-than-urgent work
     * consider using a work item.
     */
    void SendMail(CpuId who, SmpMail* mail);

    /* Order all the cpus in `who` to flush a range of vaddrs from their local
     * TLBs. If `sync` is true, this function will spin until all cpus have
     * completed acknowledged and completed the flush. If `sync` is false,
     * the function returns after the work is queued on the remote cpus and no
     * guarantee is made about when the TLB flushes will occur.
     */
    void FlushRemoteTlbs(sl::Span<CpuId> who, FlushRequest* what, bool sync);

    /* Set the hardware-specific id for the local cpu, usually the ID of the
     * cpu-local interrupt controller.
     */
    void SetMyIpiId(void* id);

    /* Returns the value set by the latest call to `SetMyIpiId()`.
     */
    void* GetIpiId(CpuId id);

    /* Send an IPI to a remote cpu with no further instructions.
     * This is useful as a building block of other operations, as it forces
     * the remote cpu to run through an interrupt entry and exit cycle.
     */
    void NudgeCpu(CpuId who);

    /* Attempts to freeze all other cpus in the system. Upon success it will
     * returns the number of frozen cpus +1 (read: total number of cpus in the
     * system, since current cpu isn't counted as being frozen). Once frozen,
     * `RunOnFrozenCpus()` can be used to execute commands across all cpus, and
     * `ThawAllCpus()` must be called to resume normal system operation.
     * Calling this function does not modify the local IPL.
     * If another cpu has already begun a freeze, the behaviour depends on
     * `allowDefer`. If `allowDefer` is set, this function will let the current
     * cpu become frozen and will try to initiate a freeze again after becoming
     * thawed. If `allowDefer` is cleared, this functions returns immediately
     * with a value of 0, indicating no cpus were frozen. The caller should
     * ensure that this cpu eventually ends up frozen (lowering IPL is often
     * enough).
     */
    size_t FreezeAllCpus(bool allowDefer);

    /* Unfreezes all other cpus in the system, enabling them to continue normal
     * execution.
     */
    void ThawAllCpus();

    /* Synchronously runs a function on all frozen cpus. If no cpus are frozen
     * (`FreezeAllCpus()` has not been called) this function does nothing.
     * This function is not reentrant, but can practically only be called
     * from the cpu that called `FreezeAllCpus()`.
     * If `includeSelf` is set, `What` will also run on the local cpu.
     * The callback function must not modify the current IPL or interrupt state.
     */
    void RunOnFrozenCpus(void (*What)(void* arg), void* arg, bool includeSelf);

    /* Similar to `RunOnFrozenCpus()` but only targets a single CPU. 
     */
    void RunOnFrozenCpu(CpuId who, void (*What)(void* arg), void* arg);
    
    CycleAccount SetCycleAccount(CycleAccount who);
    void AddClockEvent(ClockEvent* event);
    bool RemoveClockEvent(ClockEvent* event);
    sl::TimePoint GetTime();
    sl::TimePoint GetTimeOffset();
    void SetTimeOffset(sl::TimePoint offset);

    SL_ALWAYS_INLINE
    sl::TimePoint GetMonotonicTime()
    {
        return HwReadTimestamp();
    }

    /* Sets the backing store used by the kernel config manager. This is usually
     * the command line provided by the bootloader, but is not required to be.
     * The backing store is referenced, not copied, so it must be available
     * until the next call to `SetConfigStore()`. The `noLog` argument can be
     * used to suppress logging during this function so that it may be called
     * before logging is initialized. Safe to call at any IPL.
     */
    void SetConfigStore(sl::StringSpan store, bool noLog);

    /* If the current config store contains an element with a matching `key`,
     * this function returns the stored value interpreted as an unsigned 
     * integer. Otherwise it returns `defaultValue`. No IPL requirement.
     */
    size_t ReadConfigUint(sl::StringSpan key, size_t defaultValue);

    /* Similar to `ReadConfigUint()` but the stored value is returned as text
     * data rather than being interpreted as an unsigned integer. If no matching
     * config element is present in the store, `defaultValue` is returned.
     * No IPL requirement.
     */
    sl::StringSpan ReadConfigString(sl::StringSpan key,
        sl::StringSpan defaultValue);

    /* Returns the physical address for the configuration root type, if that
     * type is present on this system. Can be called at any IPL.
     */
    sl::Opt<Paddr> GetConfigRoot(ConfigRootType type);

    /* Attempts to find the first ACPI table with the matching `signature`.
     * Can be called at any IPL.
     */
    sl::Opt<sl::Sdt*> GetAcpiTable(sl::StringSpan signature);

    /* If EFI runtime services are available and have been successfully
     * enabled on this system, this function returns a pointer to the runtime
     * services table in kernel virtual memory.
     * Otherwise an empty opt is returned. Can be called at any IPL.
     */
    sl::Opt<sl::EfiRuntimeServices*> GetEfiRtServices();

    /* Attempts to lookup the `struct PageInfo` for `paddr`. Note that `paddr`
     * must be a valid address of usable memory, one that originated from a 
     * call to `AllocPage()`. This function does not enforce this requirement,
     * and may return non-null but junk values if `paddr` is invalid.
     * No IPL requirement.
     */
    SL_ALWAYS_INLINE
    PageInfo* LookupPageInfo(Paddr paddr)
    {
        const size_t index = (paddr - sysDomain0.physOffset) >> PfnShift();

        return &sysDomain0.pfndb[index];
    }

    /* Inverse of `LookupPageInfo()`, also no IPL requirement.
     */
    SL_ALWAYS_INLINE
    Paddr LookupPagePaddr(PageInfo* info)
    {
        const Paddr paddr = (info - sysDomain0.pfndb) << PfnShift();

        return paddr + sysDomain0.physOffset;
    }

    /* Returns the system domain for the current cpu.
     */
    SystemDomain& MySystemDomain();

    /* Returns the kernel map (page table root) used by the current cpu.
     */
    SL_ALWAYS_INLINE
    HwMap MyKernelMap()
    {
        return MySystemDomain().kernelMap;
    }

    /* Attempts to allocate a page of usable memory. The page is filled with
     * zeroes before returning to the caller.
     * If `canFail` is set, this function will return immediately if there
     * are no pages available, and will return a nullptr. If `canFail` is false
     * the function will block until a page can be allocated.
     * Care should be taken when calling with `canFail = false`.
     */
    PageInfo* AllocPage(bool canFail);

    /* Marks a page (and its PageInfo metadata) as no longer in use and free for
     * use by the rest of the system.
     */
    void FreePage(PageInfo* page);


    /* Attempts to copy `buffer.Size()` bytes into the memory specified by
     * `buffer` from the physical memory range starting at `base`.
     * Returns the number of bytes copied, which may be less than 
     * `buffer.Size()`.
     * This function is not safe to use on non-ram types of memory as access
     * widths used are unspecified.
     */
    size_t CopyFromPhysical(Paddr base, sl::Span<char> buffer);

    /* Attempts to retrieve a mapping for access to the page at `paddr`, which
     * must be page-aligned; an unaligned address yields an invalid reference.
     * On success the returned struct will have a non-null `vaddr` field, the
     * `paddr` field contains the same value as the `paddr` argument.
     *
     * The mapping only exists until the nearest page boundary either side of
     * `paddr` and until the returned struct has its destructor called.
     *
     * Note that this mechanism is only intended for general purpose memory
     * types (e.g. firmware or system provided data). It's not safe to use this
     * for accessing devices or other mmio as no caching control is provided to
     * the caller.
     */
    PageAccessRef AccessPage(Paddr paddr);

    /* Manually call the destructor for a page access struct. After calling
     * this function `*ref` is considered invalid and should not be used.
     */
    void DestroyPageAccess(PageAccessRef* ref);

    /* Sugar function: Gets the paddr for `page` and calls `AccessPage(paddr)`.
     */
    SL_ALWAYS_INLINE
    PageAccessRef AccessPage(PageInfo* page)
    {
        return AccessPage(LookupPagePaddr(page));
    }

    /* Updates the relative performance and efficiency values for a specific
     * cpu. These values server as hints to the scheduler.
     * This function is intended for use by the hardware layer or system specific
     * drivers that can obtain this information. The set details remain until
     * this function is called again or the system is reset, they do remain
     * across sleep states and individual cores being powered on/off.
     */
    void SetCpuPerformanceData(CpuId who, uint8_t performance,
        uint8_t efficiency);

    /*
     */
    NpkStatus ResetThread(ThreadContext* thread);

    /*
     */
    NpkStatus PrepareThread(ThreadContext* thread, uintptr_t entry, 
        uintptr_t arg, uintptr_t stack, sl::Opt<CpuId> affinity);

    /* Must be called at passive IPL with no locks held. This function does not
     * return and terminates the current thread. The exit code (`code`) is
     * stored for later retrieval, and is opaque to this function (i.e it's just
     * data, no observable changes).
     */
    [[noreturn]]
    void ExitThread(size_t code);

    /* Must be called at passive IPL. Voluntarily ends the current thread's
     * control of the current cpu, this function will eventually return when
     * the thread later resumes execution. Note that this is not a wait, no
     * control over when the thread resumes is provided.
     */
    void Yield();

    /* Must be called at passive or dpc IPL. This function moves `thread` from
     * the standby state to the ready state, and prepares it for execution by
     * placing into a cpu's run queue.
     */
    void EnqueueThread(ThreadContext* thread);

    /* Sets the niceness value for a thread. See `MinNiceness`, `MaxNiceness`,
     * and `BaseNiceness` for the meanings of nice values.
     */
    void SetThreadNiceness(ThreadContext* thread, uint8_t value);

    /* Sets the base priority of a thread, see `xyzPriority` values at the top
     * of Core.hpp for related info.
     */
    void SetThreadPriority(ThreadContext* thread, uint8_t value);

    /* Pins a thread a thread to the specified cpu. Pinning may not take effect
     * until the next time the thread is executed, e.g. if the thread is
     * currently running and has disabled preemption it will not migrate cpus
     * until it re-enables preemption.
     */
    void SetThreadAffinity(ThreadContext* thread, CpuId who);

    /* Sets the power *hint* for a thread, which affects which effects the
     * preferred cpus when scheduling this thread. Changes to this hint may not
     * take effect immediately.
     * Must be called at passive IPL.
     */
    void SetThreadPowerHint(ThreadContext* thread, PowerHint hint);

    /* Removes the pinned status of `thread`, allowing it migrate cpus again.
     */
    void ClearThreadAffinity(ThreadContext* thread);

    /* Attempts to get the current niceness value of `thread`.
     */
    sl::Opt<uint8_t> GetThreadNiceness(ThreadContext* thread);

    /* Attempts to get the current base priority of `thread`. This is the value
     * previously set by a call to `SetThreadPriority()` and may be lower
     * than the thread's effective priority.
     */
    sl::Opt<uint8_t> GetThreadPriority(ThreadContext* thread);

    /* Attempts to get the current effective priority of `thread`. This may be
     * base priority or a higher value based on priority boosts applied to
     * the thread.
     */
    sl::Opt<uint8_t> GetThreadEffectivePriority(ThreadContext* thread);

    /* Attempts to get cpu affinity and pinned status for `thread`. The valid
     * state of the return value also applies to `pinned`. If `pinned` is
     * cleared upon return, the affinity value is a hint of where the thread
     * may execute next, based on the last cpu it ran on.
     */
    sl::Opt<CpuId> GetThreadAffinity(ThreadContext* thread, bool& pinned);

    /* Attempts to get the current power hint for `thread`.
     */
    sl::Opt<PowerHint> GetThreadPowerHint(ThreadContext* thread);

    /* Attempts to cancel a preparing or ongoing wait operation for a thread.
     * Returns whether a wait was successfully cancelled or not. Waiting
     * threads will be woken with an `Aborted` status.
     * Safe to call at any IPL.
     */
    NpkStatus CancelWait(ThreadContext* thread);

    /* Must be called from passive IPL. Blocks the calling thread until the
     * waitables in `what` can be acquired, or until `timeout` expires.
     * The `entries` array must be caller-allocated with at least `what.Size()`
     * elements. For `SxMutex` type waitables, the corresponding entry's
     * `isExclusive` field must be pre-set by the caller before this call.
     * The `timeout` argument is a relative duration, passing `sl::NoTimeout`
     * blocks indefinitely. A zero-tick count makes a single non-blocking
     * acquisition attempt, returning `Timeout` if no waitable can be acquired
     * immediately.
     * The `reason` argument is an optional debug label recorded in the thread's
     * wait state while blocked.
     * Returns `Timeout` if the deadline expired before any waitable was
     * acquired, `Aborted` if the wait was cancelled via `CancelWait()`, or
     * `Reset` if a waitable was reset while the thread was blocked.
     */
    NpkStatus WaitMany(sl::Span<Waitable*> what, WaitEntry* entries,
        sl::TimeCount timeout, sl::StringSpan reason = {});

    /* Must be called from passive IPL. Convenience wrapper around `WaitMany()`,
     * all other notes are identical.
     */
    NpkStatus WaitOne(Waitable* what, WaitEntry* entry, sl::TimeCount timeout,
        sl::StringSpan reason = {});

    /* Must be called from passive IPL. Prepares `what` for use as a condition,
     * initializing it with `tickets` as the countdown value. Any threads
     * currently waiting on this object are woken with a `Reset` status.
     * While the ticket count is non-zero, waiting threads block; once it
     * reaches zero (via calls to `SetCondition()`) all current and future
     * waiters are immediately satisfied until the condition is reset again.
     * Returns `Busy` if the waitable is currently held as a mutex or sxmutex
     * and cannot safely be repurposed.
     */
    NpkStatus ResetCondition(Condition* what, size_t tickets);

    /* Must be called from passive IPL. Prepares `what` for use as a timer,
     * recording `expiry` as the initial absolute fire time. Any threads
     * currently waiting are woken with a `Reset` status. The timer does not
     * begin counting until `SetTimer()` is called; `ResetTimer()` only
     * establishes the type and initial state.
     * An optional `dpc` callback will be run at `Ipl::Dpc` when the timer
     * fires, alongside waking any waiting threads. Pass null if no extra
     * action is needed at fire time.
     * Returns `Busy` if the waitable is currently held and cannot be reset.
     */
    NpkStatus ResetTimer(Timer* what, sl::TimePoint expiry, Dpc* dpc);

    /* Must be called from passive IPL. Prepares `what` for use as a blocking
     * mutex (semaphore), setting `tickets` as the initial number of available
     * acquisitions. Any threads currently waiting are woken with a `Reset`
     * status.
     * Returns `Busy` if the waitable is currently held as a mutex.
     */
    NpkStatus ResetMutex(Mutex* what, size_t tickets);

    /* Must be called from passive IPL. Prepares `what` for use as a shared/
     * exclusive mutex, initializing it to the unheld state. Any threads
     * currently waiting are woken with a `Reset` status.
     * Returns `Busy` if the sxmutex is currently held in any mode (shared or
     * exclusive) and cannot safely be reset.
     */
    NpkStatus ResetSxMutex(SxMutex* what);

    /* Can be called from any IPL. Decrements the ticket count of `what` by
     * `count`. If this brings the count to exactly zero, all waiting threads
     * are woken and future waits will be satisfied immediately until the
     * condition is reset. If the subtraction does not reach zero, no threads
     * are woken; callers must signal the condition exactly as many times as
     * required to exhaust the ticket count set at reset.
     */
    void SetCondition(Condition* what, size_t count = 1);

    /* Can be called from any IPL. Sets the ticket count of `what` to value.
     * If `value` is zero, all waiting threads are woken and future waits will
     * be immediately satisfied until the condition is reset. If `value` is
     * non-zero no threads are woken and no waits are satisfied.
     */
    void SetConditionTo(Condition* what, size_t value);

    /* Must be called from passive IPL. Arms `what` to fire at the expiry
     * recorded during `ResetTimer()`. If `expiry` has a value it overrides
     * the previously stored expiry before arming.
     * Has no effect if the timer has already fired; it must be reset via
     * `ResetTimer()` before it can be armed again.
     */
    void SetTimer(Timer* timer, sl::Opt<sl::TimePoint> expiry);

    /* Must be called from passive IPL. Blocks the calling thread until
     * `mutex` can be acquired, or until `timeout` expires. On success the
     * calling thread becomes the recorded owner.
     * The `timeout` and `reason` arguments have the same semantics as in
     * `WaitMany()`.
     */
    NpkStatus AcquireMutex(Mutex* mutex, sl::TimeCount timeout,
        sl::StringSpan reason = {});

    /* Can be called from any IPL. Releases `mutex` and wakes at most one
     * waiting thread, which will then compete to acquire it.
     */
    void ReleaseMutex(Mutex* mutex);

    /* Must be called from passive IPL. Acquires `mutex` for shared access.
     * Multiple threads may hold an sxmutex shared simultaneously. Blocks if
     * an exclusive holder exists, or if there are pending exclusive waiters
     * and this thread is not yet queued (to prevent starvation of exclusive
     * waiters).
     * The `timeout` and `reason` arguments have the same semantics as in
     * `WaitMany()`.
     */
    NpkStatus AcquireSxMutexShared(SxMutex* mutex, sl::TimeCount timeout,
        sl::StringSpan reason = {});

    /* Must be called from passive IPL. Acquires `mutex` for exclusive access.
     * Only one thread may hold an sxmutex exclusive at a time, and it must
     * wait until both the exclusive-held flag and all shared holders are clear.
     * The `timeout` and `reason` arguments have the same semantics as in
     * `WaitMany()`.
     */
    NpkStatus AcquireSxMutexExclusive(SxMutex* mutex, sl::TimeCount timeout,
        sl::StringSpan reason = {});

    /* Can be called from any IPL. Releases one shared hold on `mutex`. If this
     * was the last holder (no other shared holds and no exclusive hold), 
     * waiting threads will be considered for wakeup.
     */
    void ReleaseSxMutexShared(SxMutex* mutex);

    /* Can be called from any IPL. Releases the exclusive hold on `mutex` and
     * wakes waiting threads.
     */
    void ReleaseSxMutexExclusive(SxMutex* mutex);
}

#define CPU_LOCAL(T, id) SL_TAGGED(cpulocal, Npk::CpuLocal<T> id)
#define NODE_LOCAL(T, id) SL_TAGGED(nodelocal, Npk::NodeLocal<T> id)

#define CPU_LOCAL_CTOR(BODY) CPU_LOCAL_CTOR2(BODY, __COUNTER__)
#define CPU_LOCAL_CTOR2(BODY, ID) CPU_LOCAL_CTOR3(BODY, ID)
#define CPU_LOCAL_CTOR3(BODY, ID) \
    static void cpu_local_ctor_##ID() BODY \
    SL_USED \
    SL_SECTION(".preinit_array", static auto* cpu_local_ctor_ptr_##ID) \
        = &cpu_local_ctor_##ID;

#define NPK_ASSERT(cond) \
    if (SL_UNLIKELY(!(cond))) \
    { \
        Npk::Panic("Assert failed %s:%i: %s, caller=%p", nullptr, \
            SL_FILENAME_MACRO, __LINE__, #cond, SL_RETURN_ADDR); \
    }

#define NPK_UNREACHABLE() \
    do \
    { \
        NPK_ASSERT(!"Unreachable code reached."); \
        SL_UNREACHABLE(); \
    } \
    while (false)

#define NPK_CHECK(cond, ret) \
    if (SL_UNLIKELY(!(cond))) \
    { \
        Npk::Log("Check failed %s:%i: %s, caller=%p", LogLevel::Error, \
            SL_FILENAME_MACRO, __LINE__, #cond, SL_RETURN_ADDR); \
        return ret; \
    }

#define NPK_ASSERT_STRINGIFY2(x) #x
#define NPK_ASSERT_STRINGIFY(x) NPK_ASSERT_STRINGIFY2(x)
#define NPK_WAIT_LOCATION \
    SL_FILENAME_MACRO ":" NPK_ASSERT_STRINGIFY(__LINE__)

#define NPK_UNEXPECTED_STATUS(status, lvl) \
    Log("(" SL_FILENAME_MACRO ":" NPK_ASSERT_STRINGIFY(__LINE__) \
        ") Unexpected status code %zu, %s", lvl, \
    status, StatusStr(status))
