#include <Userland.h>
#include <SyscallFunctions.h>
#include <WindowServerClient.h>

void Main()
{
    np::Gui::WindowServerClient client;

    //this is one way to test ipc haha.. 
    for (size_t i = 0; i < 10; i++)
        np::Gui::Window window(&client, { i, i }, "Hello example!");

    while (client.KeepGoing())
    {
        client.ProcessEvent();
        np::Syscall::Sleep(0, true);
    }
}

extern "C"
{
    void _start()
    { 
        np::Userland::InitUserlandApp();
        Main();
        np::Userland::ExitUserlandApp();
    }
}
