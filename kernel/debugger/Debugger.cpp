#include <debugger/Debugger.hpp>
#include <debugger/ProtocolGdb.hpp>
#include <Core.hpp>

/* Kernel Debugger:
 * There's three main moving parts here:
 * - transport, provided by the arch/platform layer. Used to get bytes to and from the host machine.
 * - protocol, interprets and fulfills commands from the host.
 * - core, provides the public API for the rest of the kernel and common functionality
 *   between protocols and machines.
 *
 * The debugger core mostly lives within `DispatchDebugEvent()`, 
 *
 * - allowedEvents bitmap, saves some overhead
 * - freezing protocol
 * - core/protocol operates in lockstep with host
 *   TODO: write this out properly
 */
namespace Npk::Debugger
{
    static bool initialized = false;
    static bool connected;
    static size_t cpuCount;
    static DebugProtocol* activeProtocol;
    static DebugTransport* activeTransport;
    
    static sl::Atomic<size_t> freezingCount;
    static sl::Flags<EventType> allowedEvents;

    DebugError Initialize(size_t numCpus)
    {
        if (!ReadConfigUint("npk.debugger.enable", false))
        {
            Log("Debugger not initialized: not enabled in config", 
                LogLevel::Info);
            return DebugError::NotSupported;
        }
        Log("Debugger enabled, initializing ...", LogLevel::Info);

        const auto protocolChoice = ReadConfigString("npk.debugger.protocol", 
            "gdb"_span);
        if (protocolChoice == "gdb"_span)
            activeProtocol = GetGdbProtocol();
        else
        {
            Log("Unknown debugger protocol chosen (%.*s), aborting debugger init",
                LogLevel::Error, (int)protocolChoice.Size(), protocolChoice.Begin());
            return DebugError::BadEnvironment;
        }

        if (activeTransport == nullptr)
        {
            Log("Debugger not initialized: no transport available", 
                LogLevel::Error);
            return DebugError::BadEnvironment;
        }
        activeProtocol->transport = activeTransport;

        cpuCount = numCpus;
        connected = false;
        freezingCount = 0;
        allowedEvents.Reset();
        allowedEvents.Set(EventType::RequestConnect);
        initialized = true;

        Log("Debugger initialized: transport=serial, protocol=%s.", LogLevel::Info, 
            activeProtocol->name);

        if (ReadConfigUint("npk.debugger.auto_connect", true))
        {
            Log("Debugger auto-connect is enabled, waiting for host...", LogLevel::Verbose);
            return Connect();
        }
        return DebugError::Success;
    }

    DebugError Connect()
    {
        if (!initialized)
            return DebugError::NotSupported;
        if (!allowedEvents.Has(EventType::RequestConnect))
            return DebugError::NotSupported;

        return ArchCallDebugger(EventType::RequestConnect, nullptr);
    }

    void Disconnect()
    {
        if (!initialized)
            return;
        if (!allowedEvents.Has(EventType::RequestDisconnect))
            return;

        ArchCallDebugger(EventType::RequestDisconnect, nullptr);
    }

    void NotifyOfEvent(EventType type, void* eventData)
    {
        if (!initialized)
            return;

        if (freezingCount.Load(sl::SeqCst) != 0)
            freezingCount.Sub(1, sl::SeqCst);
        while (freezingCount.Load(sl::SeqCst) != 0)
            sl::HintSpinloop();

        //check if the debugger cares about this event type right now
        if (!allowedEvents.Has(type))
            return;

        ArchCallDebugger(type, eventData);
    }

    void AddTransport(DebugTransport* transport)
    {
        if (initialized)
            return;

        activeTransport = transport;
    }

    static const char* EventTypeStr(EventType type)
    {
        switch (type)
        {
        case EventType::RequestConnect: return "request-connect";
        case EventType::RequestDisconnect: return "request-disconnect";
        case EventType::AddTransport: return "add-transport";

        case EventType::CpuException: return "exception";
        case EventType::Interrupt: return "interrupt";
        case EventType::Ipi: return "ipi";
        default:
            return "n/a";
        }
    }

    /* Freezing protocol:
     * Set `freezingCount` to the number of cpus in the system, then notify other
     * cpus via an IPI. Remote cpus will then notify the debugger of an IPI occuring,
     * at which point we check the value of `freezingCount`. If it's non-zero, that cpu
     * decrements `freezingCount` and spins until it is set to 0. The cpu that initiated
     * the freeze waits until it reaches 1, then it can continue with debugging.
     * To thaw all cpus, setting `freezingCount` to 0 will let them continue.
     * There is a race condition with this during system startup, where remote cpus
     * might not have populated their 'ipi id' fields, so we cant notify them. The
     * workaround is to re-send the ipi after an amount of time (10ms by default).
     */
    static void FreezeAllCpus()
    {
        freezingCount.Store(cpuCount, sl::SeqCst);

        const auto pingInterval = 10_ms;
        auto startTime = PlatReadTimestamp();
        auto endTime = startTime.epoch;
        do
        {
            if (PlatReadTimestamp().epoch >= endTime)
            {
                for (size_t i = 0; i < cpuCount; i++)
                    PlatSendIpi(GetIpiId(i));
                startTime = PlatReadTimestamp();
                endTime = pingInterval.Rebase(startTime.Frequency).ticks + startTime.epoch;
            }
        }
        while (freezingCount.Load(sl::SeqCst) != 1);
    }

    static void ThawAllCpus()
    {
        freezingCount.Store(0, sl::SeqCst);
    }
}

namespace Npk
{
    using namespace Debugger;

    /* This function is the heart of the debugger: it must not rely on any external kernel architecture,
     * if it did so we wouldnt be able to debug that code and its dependencies.
     * It takes in request packets (represented as type + data), and processes commands
     * from the debug host until instructed to continue.
     */
    DebugError DispatchDebugEvent(EventType type, void* data)
    {
        if (!Debugger::initialized)
            return DebugError::NotSupported;

        auto prevCycleAccount = SetCycleAccount(CycleAccount::Debugger);
        FreezeAllCpus();

        Log("Handling debugger event: %s (%u)", LogLevel::Debug, EventTypeStr(type), (unsigned)type);
        DebugError result = DebugError::NotSupported;
        switch (type)
        {
        case EventType::RequestConnect:
            if (connected)
            {
                result = DebugError::InvalidArgument;
                break;
            }
            result = activeProtocol->Connect(activeProtocol);
            connected = result == DebugError::Success;

            if (connected)
            {
                allowedEvents.Clear(EventType::RequestConnect);
                allowedEvents.Set(EventType::RequestDisconnect);
                allowedEvents.Set(EventType::CpuException);
                allowedEvents.Set(EventType::Interrupt);
                allowedEvents.Set(EventType::Ipi);
            }
            break;

        case EventType::RequestDisconnect:
            if (!connected)
            {
                result = DebugError::InvalidArgument;
                break;
            }
            activeProtocol->Disconnect(activeProtocol);
            //TODO: flush and pending requests, reset breakpoints
            result = DebugError::Success;
            break;

        default:
            NPK_UNREACHABLE(); //TODO: not this! we cant use Panic() from inside the debugger core
        }

        ThawAllCpus();
        SetCycleAccount(prevCycleAccount);

        Log("Debugger event result: %u", LogLevel::Debug, result);
        return result;
    }
}
