#include <Userland.h>
#include <SyscallFunctions.h>

void Main()
{
    using namespace np::Syscall;

    uint64_t data[] = { 0, 100, 100, 100, 100, 0, 0xF, 0 };
    for (size_t i = 0; i < 10; i++)
    {
        data[3] = 100 + i * 64;
        data[4] = 100 + i * 16;
        while (!PostToMailbox("WindowServer/Incoming", {(void*)data, sizeof(data)}))
            for (size_t j = 0; j < 10000; j++);
    }

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
