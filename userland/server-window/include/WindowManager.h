#pragma once

#include <Renderer.h>
#include <WindowDescriptor.h>
#include <NativePtr.h>
#include <Protocol.h>

namespace WindowServer
{
    class WindowManager
    {
    private:
        Renderer compositor;
        sl::Vector<WindowDescriptor*> windows;
        bool keepRunning;

        sl::NativePtr listenerBuffer;
        size_t listenerIpcHandle;

        WindowManager();

        void ProcessPacket();
        void CreateWindow(const CreateWindowRequest* request, ProtocolControlBlock* control);
        void DestroyWindow(const DestroyWindowRequest* request, ProtocolControlBlock* control);
        void MoveWindow(const MoveWindowRequest* request, ProtocolControlBlock* control);
        void ResizeWindow(const ResizeWindowRequest* request, ProtocolControlBlock* control);

    public:
        static void Run();
    };
}
