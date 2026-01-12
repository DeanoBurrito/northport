#include <DebuggerPrivate.hpp>
#include <Core.hpp>

namespace Npk
{
    static DebugStatus HandleBreakpoint(DebugProtocol& proto, void* data)
    {
        auto arg = static_cast<BreakpointEventArg*>(data);

        const auto bp = Private::GetBreakpointByAddr(arg->addr);
        if (bp == nullptr)
            return DebugStatus::InvalidBreakpoint;

        return proto.BreakpointHit(&proto, bp, arg->frame);
    }

    extern "C" 
    DebugStatus DebugEventOccured(DebugEventType what, void* data)
    {
        if (!Private::debuggerInitialized)
        {
            if (what != DebugEventType::Init)
                return DebugStatus::NotSupported;

            DebugStatus result = DebugStatus::Success;

            FreezeAllCpus(true);
            RunOnFrozenCpus(Private::DebuggerPerCpuInit, &result, true);
            ThawAllCpus();

            //TODO: init memory for debugger allocators to use
            if (result == DebugStatus::Success)
                Private::debuggerInitialized = true;
            return result;
        }

        auto prevCycleAccount = SetCycleAccount(CycleAccount::Debugger);
        Private::debugCpusCount = FreezeAllCpus(true);
        auto proto = Private::debugProtocol;

        auto result = DebugStatus::NotSupported;
        switch (what)
        {
        case DebugEventType::Connect:
            Private::debugTransportsLock.Lock();
            result = proto->Connect(proto, &Private::debugTransports);
            Private::debugTransportsLock.Unlock();
            break;

        case DebugEventType::Breakpoint:
            result = HandleBreakpoint(*proto, data);
            break;

        default:
            Log("Unknown debug event: %u", LogLevel::Error, (unsigned)what);
            break;
        }

        ThawAllCpus();
        SetCycleAccount(prevCycleAccount);

        return result;
    }
}
