#pragma once

#include <hardware/Arch.hpp>
#include <hardware/Plat.hpp>
#include <Span.h>
#include <Time.h>
#include <Flags.h>
#include <Locks.h>
#include <containers/List.h>
#include <containers/Queue.h>
#include <Compiler.h>
#include <Time.h>

extern "C" char KERNEL_CPULOCALS_BEGIN[];

namespace Npk
{
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
        Clock,
        Interrupt,
    };

    template<Ipl min, Ipl max = min>
    class IplSpinLock
    {
    private:
        sl::SpinLock lock;
        Ipl prevIpl;

    public:
        constexpr IplSpinLock() : lock {}, prevIpl {}
        {}

        inline void Lock();
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

    struct ClockEvent
    {
        Dpc* dpc;
        sl::ListHook hook;
        CpuId cpu;
        sl::TimePoint expiry;
    };
    static_assert(offsetof(ClockEvent, dpc) == 0);

    using ClockQueue = sl::List<ClockEvent, &ClockEvent::hook>;

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
    using RemoteFlushRequest = ShootdownQueue::Item;

    struct RemoteCpuStatus
    {
        sl::Atomic<bool> ipiPending;
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

    struct SmpControl;

    struct MemoryDomain
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
}

#define CPU_LOCAL(T, id) SL_TAGGED(cpulocal, Npk::CpuLocal<T> id)
