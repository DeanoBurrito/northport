#include <Userland.h>
#include <WindowManager.h>

extern "C"
{
    void _start()
    { 
        np::Userland::InitUserlandApp();
        WindowServer::WindowManager::Run();
    }
}
