#include <Core.hpp>
#include <Debugger.hpp>

namespace Npk
{
    [[noreturn]]
    void Panic(sl::StringSpan message)
    {
        while (FreezeAllCpus() == 0)
            sl::HintSpinloop();

        IntrsOff();
        Log("Panic! %.*s", LogLevel::Debug, (int)message.Size(), message.Begin());

        ConnectDebugger();
        DebugBreakpoint();

        while (true)
            WaitForIntr();
    }
}
