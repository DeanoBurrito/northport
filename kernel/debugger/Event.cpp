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

        auto result = DebugStatus::NotSupported;
        switch (what)
        {
        default:
            Log("Unknown debug event: %u", LogLevel::Error, what);
            break;
        }

        ThawAllCpus();
        SetCycleAccount(prevCycleAccount);

        return result;
    }
}
