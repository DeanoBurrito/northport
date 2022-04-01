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

    size_t streamSize = 0x900;
    sl::NativePtr bufferAddr = nullptr;
    auto maybeRid = np::Syscall::StartIpcStream("startup/listener", np::Syscall::IpcStreamFlags::UseSharedMemory, streamSize, bufferAddr.raw);
    if (maybeRid)
        terminal.PrintLine("Successfully started ipc stream.");
    else
        terminal.PrintLine("Could not start ipc stream.");

    if (bufferAddr.ptr == nullptr)
    {
        terminal.PrintLine("Startup has nullptr buffer address");
        Exit(terminal);
    }
    while (sl::MemRead<uint64_t>(bufferAddr) != 0x1234);
    terminal.PrintLine("Startup got received ipc.");
    sl::MemWrite<uint64_t>(bufferAddr, 0x4321);
    terminal.PrintLine("Startup has written ipc response.");

    // np::Syscall::StopIpcStream(*maybeRid);
    // terminal.PrintLine("Closed ipc stream.");

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
