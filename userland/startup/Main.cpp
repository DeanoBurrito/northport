#include <Userland.h>
#include <LinearFramebuffer.h>
#include <TerminalFramebuffer.h>

#include <SyscallFunctions.h>

void Exit(np::Graphics::TerminalFramebuffer& terminal)
{
    terminal.PrintLine("startup has finished. Program done.");
    while (1);
}

void Main()
{
    auto fb = np::Graphics::LinearFramebuffer::Screen();
    auto terminal = np::Graphics::TerminalFramebuffer(fb);
    terminal.PrintLine("startup.elf has loaded.");

    auto status = np::Syscall::GetFileInfo("/initdisk/config/startup.cfg");
    if (!status)
    {
        terminal.PrintLine("Could not find startup.cfg");
        Exit(terminal);
    }
    terminal.PrintLine("Found startup.cfg");
    auto maybeFileHandle = np::Syscall::OpenFile("/initdisk/config/startup.cfg");

    if (!maybeFileHandle)
    {
        terminal.PrintLine("Could not open file :(");
        Exit(terminal);
    }
    terminal.PrintLine("OpenFile succeeded! File contents:");
    
    uint8_t buffer[100];
    np::Syscall::ReadFromFile(*maybeFileHandle, 0, 0, buffer, 100);
    terminal.PrintLine(sl::String((const char*)buffer));

    np::Syscall::CloseFile(*maybeFileHandle);
    terminal.PrintLine("File handle is closed.");
    Exit(terminal);
}

extern "C"
{
    void _start()
    { 
        np::Userland::InitUserlandApp();
        Main();
    }
}
