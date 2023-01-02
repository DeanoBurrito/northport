#include <debug/TerminalDriver.h>
#include <debug/Terminal.h>
#include <debug/Log.h>

namespace Npk::Debug
{
    Terminal kernelTerminal;

    void TerminalWriteCallback(const char* str, size_t len)
    {
        kernelTerminal.Write(str, len);
    }

    void InitEarlyTerminal()
    {
        if (kernelTerminal.Init(DefaultTerminalStyle))
            AddEarlyLogOutput(TerminalWriteCallback);
        else
            kernelTerminal.Deinit();
    }

    //TODO: runtime driver for serial terminal device
}
