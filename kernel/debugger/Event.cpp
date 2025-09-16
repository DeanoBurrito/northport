#include <DebuggerPrivate.hpp>
#include <Core.hpp>

namespace Npk
{
    extern "C" 
    DebugStatus DebugEventOccured(DebugEventType what, void* data)
    {
        if (!Private::debuggerInitialized)
            return DebugStatus::NotSupported;

        auto prevCycleAccount = SetCycleAccount(CycleAccount::Debugger);
        FreezeAllCpus();

        auto proto = Private::debugProtocol;

        auto result = DebugStatus::NotSupported;
        switch (what)
        {
        case DebugEventType::Connect:
            Private::debugTransportsLock.Lock();
            result = proto->Connect(proto, &Private::debugTransports);
            Private::debugTransportsLock.Unlock();
            break;

        default:
            Log("Unknown debug event: %u", LogLevel::Error, what);
            break;
        }

        ThawAllCpus();
        SetCycleAccount(prevCycleAccount);

        return result;
    }
}
