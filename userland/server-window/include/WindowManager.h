#pragma once

#include <Renderer.h>
#include <protocols/ProtocolClient.h>
#include <BufferView.h>
#include <WindowServerProtocol.h>

namespace WindowServer
{
    class IpcProtocol;
    
    class WindowManager
    {
    private:
        IpcProtocol* ipcProtocol;
        Renderer compositor;
        bool keepRunning;

        sl::Vector<WindowDescriptor*> windows;
        sl::Vector<sl::UIntRect> damageRects;
        sl::Vector2i cursorPos;

        WindowManager();

    public:
        static void Run();

        void ProcessPacket(const ProtocolClient from, sl::BufferView packet);
        void ProcessNewClient(const ProtocolClient client, const np::Gui::NewClientRequest* request);
    };
}
