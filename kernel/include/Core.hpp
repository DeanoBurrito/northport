#pragma once

#include <Types.hpp>
#include <hardware/Arch.hpp>
#include <hardware/Plat.hpp>
#include <containers/List.hpp>
#include <containers/LruCache.hpp>
#include <containers/Queue.hpp>
#include <Locks.hpp>
#include <Span.hpp>
#include <Flags.hpp>

extern "C" char KERNEL_CPULOCALS_BEGIN[];

namespace Npk
{
    constexpr uint8_t MinPriority = 0;
    constexpr uint8_t MaxPriority = 255;
    constexpr uint8_t IdlePriority = 0;
    constexpr uint8_t MinRtPriority = MaxPriority / 2;
    constexpr uint8_t MaxRtPriority = MaxPriority;
    constexpr uint8_t MinTsPriority = IdlePriority + 1;
    constexpr uint8_t MaxTsPriority = MinRtPriority - 1;
    constexpr uint8_t MinNiceness = 0;
    constexpr uint8_t BaseNiceness = 20;
    constexpr uint8_t MaxNiceness = 39;

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

    enum class Ipl : uint8_t
    {
        Passive,
        Dpc,
        Interrupt,
    };

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

    struct InitState
    {
        uintptr_t dmBase;

        uintptr_t vmAllocHead;
        Paddr pmAllocHead;
        size_t pmAllocIndex;
        size_t usedPages;

        inline char* VmAlloc(size_t length)
        {
            const uintptr_t ret = vmAllocHead;
            vmAllocHead += AlignUpPage(length);

            return reinterpret_cast<char*>(ret);
        }

        char* VmAllocAnon(size_t length);
        Paddr PmAlloc();
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
        KernelMap kernelSpace;
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

    struct Sdt;

    enum class ThreadState : uint8_t
    {
        Dead,
        Standby,
        Ready,
        Executing,
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
            ArchThreadContext* context;

            CpuId affinity;
            sl::TimePoint sleepBegin;
            uint32_t sleepTime;
            uint32_t runTime;
            uint8_t basePriority;
            uint8_t dynPriority;
            uint8_t score;
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
    void Panic(sl::StringSpan message);

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
            Panic("Bad IPL when acquiring IplSpinLock");

        if (prevIpl < max)
            RaiseIpl(max);
        lock.Lock();
    }

    template<Ipl max, Ipl min>
    inline bool IplSpinLock<max, min>::TryLock()
    {
        auto lastIpl = CurrentIpl();
        if (lastIpl > max || min > lastIpl)
            Panic("Bad IPL when trying to acquire IplSpinLock");

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

    void QueueDpc(Dpc* dpc);
    RemoteCpuStatus* RemoteStatus(CpuId who);
    void SendMail(CpuId who, SmpMail* mail);
    void FlushRemoteTlbs(sl::Span<CpuId> who, FlushRequest* what, bool sync);
    void SetMyIpiId(void* id);
    void* GetIpiId(CpuId id);
    void NudgeCpu(CpuId who);
    
    CycleAccount SetCycleAccount(CycleAccount who);
    void AddClockEvent(ClockEvent* event);
    bool RemoveClockEvent(ClockEvent* event);
    sl::TimePoint GetTime();
    sl::TimePoint GetTimeOffset();
    void SetTimeOffset(sl::TimePoint offset);

    SL_ALWAYS_INLINE
    sl::TimePoint GetMonotonicTime()
    {
        return PlatReadTimestamp();
    }

    void SetConfigStore(sl::StringSpan store, bool noLog);
    size_t ReadConfigUint(sl::StringSpan key, size_t defaultValue);
    sl::StringSpan ReadConfigString(sl::StringSpan key, 
        sl::StringSpan defaultValue);

    sl::Opt<Paddr> GetConfigRoot(ConfigRootType type);
    sl::Opt<Sdt*> GetAcpiTable(sl::StringSpan signature);

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

    SystemDomain& MySystemDomain();

    SL_ALWAYS_INLINE
    KernelMap* MyKernelMap()
    {
        return &MySystemDomain().kernelSpace;
    }

    PageInfo* AllocPage(bool canFail);
    void FreePage(PageInfo* page);

    using PageAccessCache = sl::LruCache<Paddr, void*, Private::PmaCacheSetEntry>;
    using PageAccessRef = PageAccessCache::CacheRef;

    size_t CopyFromPhysical(Paddr base, sl::Span<char> buffer);
    void InitPageAccessCache(size_t entries, uintptr_t slots);
    PageAccessRef AccessPage(Paddr paddr);

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

    void SetThreadNiceness(ThreadContext* thread, uint8_t value);
    void SetThreadPriority(ThreadContext* thread, uint8_t value);
    void SetThreadAffinity(ThreadContext* thread, CpuId who);
    void ClearThreadAffinity(ThreadContext* thread);
    sl::Opt<uint8_t> GetThreadNiceness(ThreadContext* thread);
    sl::Opt<uint8_t> GetThreadPriority(ThreadContext* thread);
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

    sl::StringSpan IplStr(Ipl which);
    sl::StringSpan ConfigRootTypeStr(ConfigRootType which);
    sl::StringSpan CycleAccountStr(CycleAccount which);
    sl::StringSpan WaitStatusStr(WaitStatus which);
    sl::StringSpan WaitableTypeStr(WaitableType which);
    sl::StringSpan LogLevelStr(LogLevel which);
    sl::StringSpan ThreadStateStr(ThreadState which);
}

#define CPU_LOCAL(T, id) SL_TAGGED(cpulocal, Npk::CpuLocal<T> id)

#define NPK_ASSERT_STRINGIFY(x) NPK_ASSERT_STRINGIFY2(x)
#define NPK_ASSERT_STRINGIFY2(x) #x

#define NPK_ASSERT(cond) \
    if (SL_UNLIKELY(!(cond))) \
    { \
        Npk::Panic("Assert failed (" SL_FILENAME_MACRO ":" \
            NPK_ASSERT_STRINGIFY(__LINE__) "): " #cond); \
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
