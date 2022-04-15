#include <Userland.h>
#include <SyscallFunctions.h>

void Main()
{
    using namespace np::Syscall;
    sl::NativePtr bufferAddr;
    auto maybeWindowListener = OpenIpcStream("WindowServer/Listener", IpcStreamFlags::UseSharedMemory, bufferAddr.raw);
    while (!maybeWindowListener)
        maybeWindowListener = OpenIpcStream("WindowServer/Listener", IpcStreamFlags::UseSharedMemory, bufferAddr.raw);
    Log("Got handle to window server, trying to open window ...", LogLevel::Verbose);
    
    for (size_t i = 0; i < 3; i++)
    {
        //now we've successfully gotten a handle, try to create a window
        sl::NativePtr request = bufferAddr.raw + 16;
        sl::StackPush<uint64_t, true>(request, 200);
        sl::StackPush<uint64_t, true>(request, 100);
        sl::StackPush<uint64_t, true>(request, i * 200 + 50);
        sl::StackPush<uint64_t, true>(request, i * 200 + 50);
        sl::StackPush<uint64_t, true>(request, 0);
        sl::StackPush<uint64_t, true>(request, 0);
        sl::StackPush<uint64_t, true>(request, 'a');
        sl::StackPush<uint64_t, true>(request, 0);

        Log("Sending CreateWindowRequest", LogLevel::Verbose);
        *bufferAddr.As<uint64_t>(8) = 0; //clear response byte so we dont accidentally read it ourselves
        *bufferAddr.As<uint64_t>() = 1;

        while (sl::MemRead<uint64_t>(bufferAddr.As<void>(8)) != 2);
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
