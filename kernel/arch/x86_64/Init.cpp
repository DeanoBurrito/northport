#include <boot/LimineTags.h>
#include <boot/CommonInit.h>
#include <debug/Log.h>

extern "C"
{
    void KernelEntry()
    {
        Npk::InitEarlyPlatform();
        Npk::InitMemory();

        Log("Kernel done for now.", LogLevel::Info);
        while (true);
    }
}
