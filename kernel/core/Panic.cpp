#include <KernelApi.hpp>

namespace Npk
{
    [[noreturn]]
    void Panic(sl::StringSpan message)
    {
        IntrsOff();
        Log("Panic! %.*s", LogLevel::Debug, (int)message.Size(), message.Begin());

        while (true)
            WaitForIntr();
    }
}
