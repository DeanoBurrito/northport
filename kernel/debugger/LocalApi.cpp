#include <DebuggerPrivate.hpp>
#include <Core.hpp>

namespace Npk
{
    namespace Private
    {
        bool debuggerInitialized = false;
        sl::Flags<DebugEventType> enabledEvents {};
        sl::SpinLock debugTransportsLock;
        DebugTransportList debugTransports {};
        DebugProtocol* debugProtocol;
    }

    static bool connected = false;

    void InitDebugger()
    {
        NPK_ASSERT(!Private::debuggerInitialized);

        if (!ReadConfigUint("npk.debugger.enable", false))
        {
            Log("Debugger not initialized: not enable in config",
                LogLevel::Info);
            return;
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
            return;
        }

        connected = false;
        Private::enabledEvents.Reset();
        Private::enabledEvents.Set(DebugEventType::Connect);

        Private::debuggerInitialized = true;

        size_t transportCount = 0;
        Private::debugTransportsLock.Lock();
        for (auto it = Private::debugTransports.Begin(); 
            it != Private::debugTransports.End(); ++it)
            transportCount++;
        Private::debugTransportsLock.Unlock();

        Log("Debugger initialized: protocol=%s, transports=%zu", LogLevel::Info,
            Private::debugProtocol->name, transportCount);

        if (ReadConfigUint("npk.debugger.auto_connect", true))
        {
            Log("Debugger auto-connected is enabled, waiting for host ...",
                LogLevel::Info);
            ConnectDebugger();
        }
    }

    DebugStatus ConnectDebugger()
    {
        if (!Private::debuggerInitialized)
            return DebugStatus::NotSupported;
        if (!Private::enabledEvents.Has(DebugEventType::Connect))
            return DebugStatus::NotSupported;

        return ArchCallDebugger(DebugEventType::Connect, nullptr);
    }

    void DisconnectDebugger()
    {
        if (!Private::debuggerInitialized)
            return;
        if (!Private::enabledEvents.Has(DebugEventType::Disconnect))
            return;

        ArchCallDebugger(DebugEventType::Disconnect, nullptr);
    }

    void AddDebugTransport(DebugTransport* transport)
    {
        auto predicate = [](DebugTransport* a, DebugTransport* b) -> bool
        {
            return a->priority < b->priority;
        };

        NPK_ASSERT(transport != nullptr);
        NPK_ASSERT(transport->Receive != nullptr);
        NPK_ASSERT(transport->Transmit != nullptr);

        Private::debugTransportsLock.Lock();
        Private::debugTransports.InsertSorted(transport, predicate);
        Private::debugTransportsLock.Unlock();

        Log("Added debug transport: %p, %s", LogLevel::Info, transport,
            transport->name);
    }
}
