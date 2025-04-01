#include <Kernel.hpp>
#include <BakedConstants.hpp>

#include <hardware/Entry.hpp>

namespace Npk
{
    void DispatchAlarm() {}
    void DispatchIpi() {}
    void DispatchInterrupt(size_t vector) {}
    void DispatchPageFault(PageFaultFrame* frame) {}
    void DispatchSyscall(SyscallFrame* frame) {}
    void DispatchException(ExceptionFrame* frame) {}
}

extern "C"
{
    void KernelEntry()
    {
        using namespace Npk;

        ArchInitEarly();
        PlatInitEarly();

        Log("Northport kernel v%zu.%zu.%zu starting ...", LogLevel::Info,
            versionMajor, versionMinor, versionRev);
        Log("Compiler flags: %s", LogLevel::Verbose, compileFlags);
        Log("Base Commit%s: %s", LogLevel::Verbose, gitDirty ? " (dirty)" : "",
            gitHash);


        Log("---- INIT DONE ----", LogLevel::Debug);
        while (true)
        {
            IntrsOff();
            WaitForIntr();
        }
    }
}
