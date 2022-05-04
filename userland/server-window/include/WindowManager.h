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
        bool keepRunning;
        size_t listenerIpcHandle;

        sl::Vector<WindowDescriptor*> windows;
        sl::Vector<sl::UIntRect> damageRects;
        sl::Vector2i cursorPos;

        WindowManager();
        void ProcessPacket(uint8_t* buffer);

        void CreateWindow(const CreateWindowRequest* request);
        // void DestroyWindow(const DestroyWindowRequest* request, ProtocolControlBlock* control);
        // void MoveWindow(const MoveWindowRequest* request, ProtocolControlBlock* control);
        // void ResizeWindow(const ResizeWindowRequest* request, ProtocolControlBlock* control);

    public:
        static void Run();
    };
}
