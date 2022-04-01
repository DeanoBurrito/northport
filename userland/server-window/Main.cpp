#include <Userland.h>
#include <LinearFramebuffer.h>
#include <TerminalFramebuffer.h>
#include <SyscallFunctions.h>

#include <SyscallFunctions.h>

void Exit(np::Graphics::TerminalFramebuffer& terminal)
{
    terminal.PrintLine("Window server has finished.", {60, 10});
    while (1);
}

void Main()
{
    using namespace np::Syscall;
    
    auto fb = np::Graphics::LinearFramebuffer::Screen();
    auto terminal = np::Graphics::TerminalFramebuffer(fb);

    terminal.PrintLine("Hello from window server", { 60, 0 });

    for (size_t i = 0; i < 0x200000; i++) //since we cant sleep from userspace yet, thisll do.
        asm("pause");

    sl::NativePtr bufferAddr = nullptr;

    auto maybeIpcHandle = OpenIpcStream("startup/listener", IpcStreamFlags::UseSharedMemory, bufferAddr.raw);
    if (!maybeIpcHandle)
    {
        terminal.PrintLine("Window server failed to open ipc stream.", {60, 1});
        Exit(terminal);
    }
    else
        terminal.PrintLine("Window server opened ipc stream.", {60, 1});

    if (bufferAddr.ptr == nullptr)
    {
        terminal.PrintLine("Window server has nullptr buffer address", {60, 2});
        Exit(terminal);
    }
    sl::MemWrite(bufferAddr, 0x1234);
    terminal.PrintLine("Window server has written value to ipc stream.", {60, 2});

    while (sl::MemRead<uint64_t>(bufferAddr) != 0x4321);
    terminal.PrintLine("Window server has received response.", {60, 3});

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
