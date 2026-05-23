#include <private/Debugger.hpp>
#include <Core.hpp>
#include <Vm.hpp>

namespace Npk
{
    using namespace Private;

    constexpr size_t MaxBreakpoints = 64;
    constexpr size_t DefaultDebuggerConnectMs = 5000;

    static sl::SpinLock debugTransportsLock;
    static DebugTransportList debugTransports;

    NpkStatus InitDebugger(uintptr_t& virtBase)
    {
        auto flags = debugEventsMask.Load(sl::Relaxed);
        NPK_ASSERT(!flags.Any());

        if (!ReadConfigUint("npk.debugger.enable", false))
        {
            Log("Debugger not initialized: not enabled in config",
                LogLevel::Info);

            return NpkStatus::BadConfig;
        }
        Log("Debugger enabled, initializing ...", LogLevel::Info);

        const auto whichProto = ReadConfigString("npk.debugger.protocol",
            "gdb"_span);
        if (whichProto == "gdb"_span)
            Private::debugProtocol = &Private::gdbProtocol;
        else
        {
            Log("Unknown debugger protocol: %.*s. Aborting debugger init",
                LogLevel::Error, (int)whichProto.Size(), whichProto.Begin());

            return NpkStatus::BadConfig;
        }

        const size_t maxBreakpoints = 
            ReadConfigUint("npk.debugger.max_breakpoints", MaxBreakpoints);
        const size_t bpBuffSize = 
            AlignUpPage(maxBreakpoints * sizeof(Breakpoint));
        const uintptr_t breakpointBuff = virtBase;
        virtBase += bpBuffSize;

        for (uintptr_t i = breakpointBuff; i != virtBase; i += PageSize())
        {
            //TODO: remove fatal failure path
            NPK_ASSERT(SetKernelMap(i, LookupPagePaddr(AllocPage(false)), 
                VmFlag::Write) == NpkStatus::Success);
        }

        const size_t logringLen = AlignUpPage(
            ReadConfigUint("npk.debugger.logring_size", PageSize()));
        const uintptr_t logringBase = virtBase;
        virtBase += logringLen;

        for (uintptr_t i = logringBase; i != virtBase; i += PageSize())
        {
            NPK_ASSERT(SetKernelMap(i, LookupPagePaddr(AllocPage(false)), 
                VmFlag::Write) == NpkStatus::Success);
        }

        const size_t cpuCount = MySystemDomain().smpControls.Size();
        const size_t perCpuSize = AlignUpPage(cpuCount * PerCpuStorePointers 
            * sizeof(uintptr_t));
        const uintptr_t perCpuBase = virtBase;
        virtBase += perCpuSize;

        for (uintptr_t i = perCpuBase; i != virtBase; i += PageSize())
        {
            NPK_ASSERT(SetKernelMap(i, LookupPagePaddr(AllocPage(false)),
                VmFlag::Write) == NpkStatus::Success);
        }

        InitEventArg arg {};
        arg.breakpoints = { reinterpret_cast<Breakpoint*>(breakpointBuff),
            bpBuffSize / sizeof(Breakpoint) };
        arg.logring = { reinterpret_cast<char*>(logringBase), logringLen };
        arg.perCpu = { reinterpret_cast<uintptr_t*>(perCpuBase), cpuCount 
            * PerCpuStorePointers };

        auto result = HwCallDebugger(DebugEventType::Init, &arg);
        if (result != NpkStatus::Success)
        {
            NPK_UNEXPECTED_STATUS(result, LogLevel::Error);

            return result;
        }

        size_t transportCount = 0;
        debugTransportsLock.Lock();
        for (auto it = debugTransports.Begin(); it != debugTransports.End(); 
            ++it)
            transportCount++;
        debugTransportsLock.Unlock();

        Log("Debugger initialized: protocol=%s, transports=%zu", LogLevel::Info,
            Private::debugProtocol->name, transportCount);

        if (ReadConfigUint("npk.debugger.auto_connect", true))
        {
            const size_t timeoutMs = 
                ReadConfigUint("npk.debugger.connect_timeout_ms", 
                DefaultDebuggerConnectMs);
            Log("Debugger auto-connect is enabled (timeout of %zu ms), "
                "waiting for host ...", LogLevel::Info, timeoutMs);

            const auto timeout = sl::TimeCount(sl::Millis, timeoutMs);
            return ConnectDebugger(timeout);
        }

        return NpkStatus::Success;
    }

    NpkStatus ConnectDebugger(sl::TimeCount timeout)
    {
        auto flags = debugEventsMask.Load(sl::Relaxed);
        if (!flags.Has(DebugEventType::Connect))
            return NpkStatus::NotAvailable;

        ConnectEventArg arg {};
        arg.timeout = timeout;
        debugTransportsLock.Lock();
        arg.transports = &debugTransports;

        const auto result = HwCallDebugger(DebugEventType::Connect, &arg);
        debugTransportsLock.Unlock();

        return result;
    }

    void DisconnectDebugger()
    {
        auto flags = debugEventsMask.Load(sl::Relaxed);
        if (!flags.Has(DebugEventType::Disconnect))
            return;

        DisconnectEventArg arg {};

        HwCallDebugger(DebugEventType::Disconnect, &arg);
    }

    void DebugBreakpoint()
    {
        auto flags = debugEventsMask.Load(sl::Relaxed);
        if (!flags.Has(DebugEventType::Breakpoint))
            return;

        BreakpointEventArg arg {};
        arg.addr = reinterpret_cast<uintptr_t>(DebugBreakpoint);
        arg.frame = nullptr;
        arg.type = BreakpointType::Manual;

        HwCallDebugger(DebugEventType::Breakpoint, &arg);
    }

    NpkStatus AddDebugTransport(DebugTransport* transport)
    {
        if (transport == nullptr)
            return NpkStatus::InvalidArg;
        if (transport->Receive == nullptr)
            return NpkStatus::InvalidArg;
        if (transport->Transmit == nullptr)
            return NpkStatus::InvalidArg;

        auto predicate = [](DebugTransport* a, DebugTransport* b) -> bool
        {
            return a->priority < b->priority;
        };

        debugTransportsLock.Lock();
        debugTransports.InsertSorted(transport, predicate);
        debugTransportsLock.Unlock();

        Log("Added debug transport: %p, %s", LogLevel::Info, transport,
            transport->name);

        return NpkStatus::Success;
    }
}
