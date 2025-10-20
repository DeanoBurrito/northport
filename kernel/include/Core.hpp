#pragma once

#include <Hardware.hpp>
#include <containers/List.hpp>
#include <containers/LruCache.hpp>
#include <containers/Queue.hpp>
#include <Locks.hpp>
#include <Efi.hpp>

extern "C" char KERNEL_CPULOCALS_BEGIN[];

namespace sl
{
    struct Sdt;
}

namespace Npk
{
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

    struct Dpc;

    using DpcEntry = void (*)(Dpc* self, void* arg);

    struct Dpc
    {
        sl::FwdListHook hook;
        DpcEntry function;
        void* arg;
    };

    using DpcQueue = sl::FwdList<Dpc, &Dpc::hook>;

    enum class CycleAccount
    {
        User,
        Kernel,
        KernelInterrupt,
        Driver,
        DriverInterrupt,
        Debugger,
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

    enum class WaitStatus : uint8_t
    {
        Incomplete,
        Timedout,
        Reset,
        Cancelled,
        Success,
    };

    enum class WaitableType : uint8_t
    {
        Condition,
        Timer,
        Mutex,
    };

    struct WaitEntry;
    struct ThreadContext;
    struct Waitable;

    struct WaitEntry
    {
        sl::ListHook waitableQueueHook;

        ThreadContext* thread;
        Waitable* waitable;
        sl::Atomic<WaitStatus> status;
    };

    struct Waitable
    {
        WaitableType type;

        IplSpinLock<Ipl::Dpc> lock;
        sl::List<WaitEntry, &WaitEntry::waitableQueueHook> waiters;

        size_t tickets;
        union
        {
            ClockEvent clockEvent;
            ThreadContext* mutexHolder;
        };
    };

    struct SmpMailData;
    using MailQueue = sl::QueueMpSc<SmpMailData>;
    using MailFunction = void (*)(void* arg);

    struct SmpMailData
    {
        MailFunction function;
        void* arg;
        Waitable* onComplete;
    };
    using SmpMail = MailQueue::Item;

    struct RemoteFlushData;
    using ShootdownQueue = sl::QueueMpSc<RemoteFlushData>;

    struct RemoteFlushData
    {
        uintptr_t base;
        size_t length;
        sl::Atomic<size_t> acknowledgements;
    };

    using FlushRequest = ShootdownQueue::Item;

    struct LocalScheduler;

    struct RemoteCpuStatus
    {
        sl::Atomic<sl::TimePoint> lastIpi;
        LocalScheduler* scheduler;
    };

    struct SmpControl
    {
        void* ipiId;
        MailQueue mail;
        ShootdownQueue shootdowns;
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
                uint32_t flags;
                uint16_t offset;
                uint16_t wireCount;
                void* vmo;
            } vm;

            struct
            {
                uint16_t validPtes;
            } mmu;
        };
    };
    static_assert(sizeof(PageInfo) <= (sizeof(void*) * 4));

    using PageList = sl::FwdList<PageInfo, &PageInfo::mmList>;

    struct SystemDomain
    {
        Paddr physOffset;
        PageInfo* pfndb;

        CpuId smpBase;
        sl::Span<SmpControl> smpControls;

        uintptr_t pmaBase;
        HwMap kernelSpace;
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
            Waitable lock;
            PageList active;
            PageList dirty;
            PageList standby;
        } liveLists;
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

        /* Thread is currently waiting on an event/waitable. When the wait is
         * completed (by satisfaction or by timing out) the thread will wake
         * and be placed in a core's run queues and move to the `Ready` state.
         */
        Waiting,
    };

    struct ThreadContext
    {
        struct 
        {
            sl::Atomic<uint64_t> userNs; //TODO: use TimePoint instead of raw nanos
            sl::Atomic<uint64_t> kernelNs;
        } accounting;

        struct
        {
            IntrSpinLock lock;
            HwThreadContext* context;

            CpuId affinity;
            sl::TimePoint sleepBegin;
            uint32_t sleepTime;
            uint32_t runTime;
            uint8_t basePriority;
            uint8_t dynPriority;
            bool isPinned;
            ThreadState state;
            bool isInteractive;
            uint8_t niceness;
        } scheduling;
        sl::ListHook queueHook; //NOTE: protected by scheduling.lock

        struct
        {
            IplSpinLock<Ipl::Dpc> lock;
            sl::Span<WaitEntry> entries;
            sl::StringSpan reason;
        } waiting;

        inline uint8_t Priority() const
        {
            if (scheduling.dynPriority != 0)
                return scheduling.dynPriority;
            return scheduling.basePriority;
        }
    };

    using ThreadQueue = sl::List<ThreadContext, &ThreadContext::queueHook>;

    namespace Private
    {
        bool PmaCacheSetEntry(size_t slot, void** curVaddr, Paddr curPaddr, 
            Paddr nextPaddr);
    };

    extern SystemDomain domain0;

    SL_PRINTF_FUNC(1, 3)
    void Log(const char* msg, LogLevel level, ...);
    [[noreturn]]
    void Panic(sl::StringSpan message, TrapFrame* frame);

    void AddLogSink(LogSink& sink);
    void RemoveLogSink(LogSink& sink);

    void AssertIpl(Ipl target);
    Ipl CurrentIpl();
    Ipl RaiseIpl(Ipl target);
    void LowerIpl(Ipl target);

    template<Ipl max, Ipl min>
    inline void IplSpinLock<max, min>::Lock()
    {
        prevIpl = CurrentIpl();
        if (prevIpl > max || min > prevIpl)
            Panic("Bad IPL when acquiring IplSpinLock", nullptr);

        if (prevIpl < max)
            RaiseIpl(max);
        lock.Lock();
    }

    template<Ipl max, Ipl min>
    inline bool IplSpinLock<max, min>::TryLock()
    {
        auto lastIpl = CurrentIpl();
        if (lastIpl > max || min > lastIpl)
            Panic("Bad IPL when trying to acquire IplSpinLock", nullptr);

        if (lastIpl < max)
            RaiseIpl(max);
        const bool success = lock.TryLock();

        if (success)
            prevIpl = lastIpl;
        else
            LowerIpl(prevIpl);

        return success;
    }

    template<Ipl max, Ipl min>
    inline void IplSpinLock<max, min>::Unlock()
    {
        if (prevIpl < max)
            LowerIpl(prevIpl);
        lock.Unlock();
    }

    /* Queues a DPC for execution on the current cpu. This function can be
     * called at any level: if run from below IPL::DPC, it will raise the local
     * ipl and execute the DPC immediately.
     */
    void QueueDpc(Dpc* dpc);

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
     * guarentee is made about when the TLB flushes will occur.
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
     * system, since current cpu isnt counted as being frozen). Once frozen,
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

    /* Sychronously runs a function on all frozen cpus. If no cpus are frozen
     * (`FreezeAllCpus()` has not been called) this function does nothing.
     * This function is not reentrant, but can practically only be called
     * from the cpu that called `FreezeAllCpus()`.
     * If `includeSelf` is set, `What` will also run on the local cpu.
     * The callback function must not modify the current IPL or interrupt state.
     */
    void RunOnFrozenCpus(void (*What)(void* arg), void* arg, bool includeSelf);
    
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

    void SetConfigStore(sl::StringSpan store, bool noLog);
    size_t ReadConfigUint(sl::StringSpan key, size_t defaultValue);
    sl::StringSpan ReadConfigString(sl::StringSpan key, 
        sl::StringSpan defaultValue);

    sl::Opt<Paddr> GetConfigRoot(ConfigRootType type);
    sl::Opt<sl::Sdt*> GetAcpiTable(sl::StringSpan signature);
    sl::Opt<sl::EfiRuntimeServices*> GetEfiRtServices();

    SL_ALWAYS_INLINE
    PageInfo* LookupPageInfo(Paddr paddr)
    {
        return &domain0.pfndb[((paddr - domain0.physOffset) >> PfnShift())];
    }

    SL_ALWAYS_INLINE
    Paddr LookupPagePaddr(PageInfo* info)
    {
        return ((info - domain0.pfndb) << PfnShift()) + domain0.physOffset;
    }

    /* Returns the system domain for the current cpu.
     */
    SystemDomain& MySystemDomain();

    /* Returns the kernel map (page table root) used by the current cpu.
     */
    SL_ALWAYS_INLINE
    HwMap MyKernelMap()
    {
        return MySystemDomain().kernelSpace;
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

    using PageAccessCache = sl::LruCache<Paddr, void*, Private::PmaCacheSetEntry>;
    using PageAccessRef = PageAccessCache::CacheRef;

    /* Attempts to copy `buffer.Size()` bytes into the memory specified by
     * `buffer` from the physical memory range starting at `base`.
     * Returns the number of bytes copied, which may be less than 
     * `buffer.Size()`.
     * This function is not safe to use on non-ram types of memory as access
     * widths used are unspecified.
     */
    size_t CopyFromPhysical(Paddr base, sl::Span<char> buffer);

    /* Attempts to create a temporary mapping to access the page at `paddr`.
     * Returns a refcounted struct where `key` is equal to `paddr`, and
     * `value` is the virtual address the page can be accessed at. While the
     * refcount is non-zero the virtual address remains valid to access.
     * The mapping is shared by all CPUs in the same system domain.
     */
    PageAccessRef AccessPage(Paddr paddr);

    /* Sugar function: Gets the paddr for `page` and calls `AccessPage(paddr)`.
     */
    SL_ALWAYS_INLINE
    PageAccessRef AccessPage(PageInfo* page)
    {
        return AccessPage(LookupPagePaddr(page));
    }

    bool ResetThread(ThreadContext* thread);
    bool PrepareThread(ThreadContext* thread, uintptr_t entry, uintptr_t arg, 
        uintptr_t stack, sl::Opt<CpuId> affinity);
    void ExitThread(size_t code, void* data);
    void Yield();
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

    void CancelWait(ThreadContext* thread);
    WaitStatus WaitMany(sl::Span<Waitable*> what, WaitEntry* entries, 
        sl::TimeCount timeout, sl::StringSpan reason = {});
    void SignalWaitable(Waitable* what);
    void ResetWaitable(Waitable* what, WaitableType newType, size_t tickets);

    SL_ALWAYS_INLINE
    WaitStatus WaitOne(Waitable* what, WaitEntry* entry, sl::TimeCount timeout,
        sl::StringSpan reason = {})
    {
        return WaitMany({ &what, 1 }, entry, timeout, reason);
    }

    /* Enum value to string function for `enum Ipl`.
     */
    sl::StringSpan IplStr(Ipl which);

    /* Enum value to string function for `enum ConfigRootType`.
     */
    sl::StringSpan ConfigRootTypeStr(ConfigRootType which);

    /* Enum value to string function for `enum CycleAccount`.
     */
    sl::StringSpan CycleAccountStr(CycleAccount which);

    /* Enum value to string function for `enum WaitStatus`.
     */
    sl::StringSpan WaitStatusStr(WaitStatus which);

    /* Enum value to string function for `enum WaitableType`.
     */
    sl::StringSpan WaitableTypeStr(WaitableType which);

    /* Enum value to string function for `enum LogLevel`.
     */
    sl::StringSpan LogLevelStr(LogLevel which);

    /* Enum value to string function for `enum ThreadState`.
     */
    sl::StringSpan ThreadStateStr(ThreadState which);
}

#define CPU_LOCAL(T, id) SL_TAGGED(cpulocal, Npk::CpuLocal<T> id)

#define NPK_ASSERT_STRINGIFY(x) NPK_ASSERT_STRINGIFY2(x)
#define NPK_ASSERT_STRINGIFY2(x) #x

#define NPK_ASSERT(cond) \
    if (SL_UNLIKELY(!(cond))) \
    { \
        Npk::Panic("Assert failed (" SL_FILENAME_MACRO ":" \
            NPK_ASSERT_STRINGIFY(__LINE__) "): " #cond, nullptr); \
    }

#define NPK_UNREACHABLE() \
    NPK_ASSERT(!"Unreachable code reached."); \
    SL_UNREACHABLE()

//TODO: print a backtrace of where the check failed?
#define NPK_CHECK(cond, ret) \
    if (SL_UNLIKELY(!(cond))) \
    { \
        Npk::Log("Check failed %s:%i: %s, caller=%p", LogLevel::Error, \
            SL_FILENAME_MACRO, __LINE__, #cond, SL_RETURN_ADDR); \
        return ret; \
    }
