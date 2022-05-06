#include <Userland.h>
#include <WindowManager.h>

extern "C"
{
    void _start()
    { 
        np::Userland::InitUserlandApp();
        WindowServer::WindowManager::Run();
        np::Userland::ExitUserlandApp(1); //this should never exit, so emit an error if we do.
    }
}
