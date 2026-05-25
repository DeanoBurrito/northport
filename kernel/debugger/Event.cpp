#include <private/Debugger.hpp>
#include <Core.hpp>

namespace Npk
{
    using namespace Private;

    static NpkStatus DoDisconnect(DebugProtocol* proto)
    {
        debugHostWantsDisconnect = false;
        proto->Disconnect(proto);
        debugEventsMask.Store(DebugEventType::Connect, sl::Release);

        return NpkStatus::Success;
    }

    extern "C"
    NpkStatus DebugEventOccurred(DebugEventType what, void* data)
    {
        if (data == nullptr)
            return NpkStatus::InvalidArg;

        auto* proto = debugProtocol;
        FreezeAllCpus(true);

        auto result = NpkStatus::Unsupported;
        switch (what)
        {
        case DebugEventType::Init:
            {
                auto* arg = static_cast<InitEventArg*>(data);
                InitInternalDebuggerApi(arg);

                sl::Atomic<NpkStatus> atomicStatus = NpkStatus::Success;
                RunOnFrozenCpus(DebuggerPerCpuInit, &atomicStatus, true);

                result = atomicStatus.Load(sl::Acquire);
                if (result == NpkStatus::Success)
                    debugEventsMask.Store(DebugEventType::Connect, sl::Release);

                break;
            }

        case DebugEventType::Connect:
            {
                auto* arg = static_cast<ConnectEventArg*>(data);

                result = proto->Connect(proto, arg->transports, arg->timeout);
                if (result == NpkStatus::Success)
                {
                    DebugEventTypes flags {};
                    flags.Set(DebugEventType::Breakpoint);
                    flags.Set(DebugEventType::Disconnect);

                    debugEventsMask.Store(flags, sl::Release);
                }
                break;
            }

        case DebugEventType::Disconnect:
            result = DoDisconnect(proto);
            break;

        case DebugEventType::Breakpoint:
            {
                auto* arg = static_cast<BreakpointEventArg*>(data);
                const auto bp = GetBreakpointByAddr(arg->addr);

                if (bp == nullptr && arg->type == BreakpointType::Breakpoint)
                {
                    result = NpkStatus::InvalidArg;
                    break;
                }
                
                result = proto->BreakpointHit(proto, arg->type, bp, arg->frame);
                break;
            }

        default:
            break;
        }

        if (debugHostWantsDisconnect)
            DoDisconnect(proto);

        ThawAllCpus();

        return result;
    }
}
