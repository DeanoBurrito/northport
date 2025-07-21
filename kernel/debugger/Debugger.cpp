#include <debugger/Debugger.hpp>
#include <debugger/GdbRemote.hpp>
#include <KernelApi.hpp>
#include <containers/List.h>
#include <Maths.h>

/* Kernel Debugger Theory:
 * - besides initialize(), actual debugger work happens inside the interrupt handler for RtlDispatchDebugRequest()
 * - operates in lockstep with debugger commands. Kernel runs until a stop condition is met and then notifies debugger.
 *      - exception to this is host sending 'STOP NOW' message, we'll need device interrupts for this.
 * - bitmap of enabled debug events, which we can check before submitting RtlDispatchDebugRequest(), saves some overhead.
 * - separation of high level debugger control, debugger protocol, and debugger transport
 */
namespace Npk::Debugger
{
    static bool initialized = false;
    static bool connected;
    static size_t cpuCount;
    static DebugProtocol* activeProtocol;
    
    static sl::Atomic<size_t> freezingCount;

    DebugError Initialize(size_t numCpus)
    {
        if (!ReadConfigUint("npk.debugger.enable", false))
        {
            Log("Debugger not initialized: not enabled in config", LogLevel::Info);
            return DebugError::NotSupported;
        }

        const auto protocolChoice = ReadConfigString("npk.debugger.protocol", "gdb");
        if (protocolChoice == "gdb"_span)
            activeProtocol = GetGdbProtocol();
        else
        {
            Log("Unknown debugger protocol chosen (%.*s), aborting debugger init",
                LogLevel::Error, (int)protocolChoice.Size(), protocolChoice.Begin());
            return DebugError::BadEnvironment;
        }
        //TODO: ensure at least one transport is available

        cpuCount = numCpus;
        initialized = true;
        connected = false;
        freezingCount = 0;

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

        return ArchCallDebugger(EventType::RequestConnect, nullptr);
    }

    void Disconnect()
    {
        if (!initialized)
            return;

        ArchCallDebugger(EventType::RequestDisconnect, nullptr);
    }

    static const char* EventTypeStr(EventType type)
    {
        switch (type)
        {
        case EventType::RequestConnect: return "request-connect";
        case EventType::RequestDisconnect: return "request-disconnect";
        default:
            return "n/a";
        }
    }

    static void FreezeAllCpus()
    {
        freezingCount.Store(cpuCount, sl::SeqCst);

        //TODO: send NMI to notify of freeze occuring
        while (freezingCount.Load(sl::SeqCst) != 1)
            sl::HintSpinloop();
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
        return result;
    }
}
