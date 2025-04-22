#pragma once

#include <hardware/Arch.hpp>
#include <hardware/Plat.hpp>
#include <Span.h>
#include <Time.h>
#include <Flags.h>
#include <Locks.h>
#include <containers/List.h>
#include <Compiler.h>
#include <Time.h>

extern "C" char KERNEL_CPULOCALS_BEGIN[];

namespace Npk
{
    struct InitState
    {
        uintptr_t dmBase;

        uintptr_t vmAllocHead;
        Paddr pmAllocHead;
        size_t pmAllocIndex;

        inline char* VmAlloc(size_t length)
        {
            const uintptr_t ret = vmAllocHead;
            vmAllocHead += AlignUpPage(length);

            return reinterpret_cast<char*>(ret);
        }

        char* VmAllocAnon(size_t length);
        Paddr PmAlloc();
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

    enum class Ipl
    {
        Passive,
        Dpc,
        Clock,
        Interrupt,
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

    enum class WaitResult
    {
        Success,
        Timeout,
        Cancelled,
        Alerted,
    };

    enum class WaitFlag
    {
        Cancellable,
        Alertable,
        All,
    };

    using WaitFlags = sl::Flags<WaitFlag>;

    struct WaitEntry;

    struct Waiter
    {
        bool cancellable;
        bool alertable;
    };

    struct WaitEntry
    {
        sl::ListHook hook;

        Waiter* waiter;
        bool satisfied;
    };

    class Waitable
    {
    private:
        size_t count : 8 * sizeof(size_t) - 1;
        bool sticky : 1;

        sl::List<WaitEntry, &WaitEntry::hook> entries;

    public:
        void Reset();
        void Signal(size_t count, bool sticky);
    };

    using Mutex = sl::SpinLock;

    using CondVar = Waitable;

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

    struct MmuSpace;

    struct MemoryDomain
    {
        Paddr physOffset;
        PageInfo* pfndb;

        uintptr_t pmaBase;
        MmuSpace* kernelSpace;
        Paddr zeroPage;
        
        CondVar highMemoryPressure;

        struct
        {
            Mutex lock;
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
    };
}

#define CPU_LOCAL(T, id) SL_TAGGED(cpulocal, Npk::CpuLocal<T> id)
