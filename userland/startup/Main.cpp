#include <Userland.h>
#include <SyscallFunctions.h>

void Main()
{
    // using namespace np::Gui;
    
    // Window window = Window::Create({ 100, 100 }, "Hello Window!");
    // Window window2 = Window::Create({ 200, 120 }, "Hello Window 2!", { 100, 100 });
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
