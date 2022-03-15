#include <Userland.h>
#include <LinearFramebuffer.h>
#include <TerminalFramebuffer.h>

#include <SyscallFunctions.h>

void Main()
{
    auto fb = np::Graphics::LinearFramebuffer::Screen();
    auto terminal = np::Graphics::TerminalFramebuffer(fb);
    terminal.PrintLine("");

    auto status = np::Syscall::GetFileInfo("/initdisk/config/startup.cfg");
    if (status)
        terminal.PrintLine("Found startup.cfg");
    else
        terminal.PrintLine("Could not find startup.cfg");

    if (status)
    {
        auto maybeFileHandle = np::Syscall::OpenFile("/initdisk/config/startup.cfg");
        if (maybeFileHandle)
            terminal.PrintLine("OpenFile succeeded! We have a file handle!");
        else
            terminal.PrintLine("Could not open file :(");
    }

    terminal.PrintLine("Program done.");
    while (1);
}

extern "C"
{
    void _start()
    { 
        np::Userland::InitUserlandApp();
        Main();
    }
}
