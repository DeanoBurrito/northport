#pragma once

#include <render/Renderer.h>
#include <protocols/ProtocolClient.h>
#include <BufferView.h>
#include <WindowServerProtocol.h>
#include <IdAllocator.h>
#include <Shell.h>
#include <Keys.h>

namespace WindowServer
{
    class IpcProtocol;
    
    class WindowManager
    {
    private:
        IpcProtocol* ipcProtocol;
        Renderer compositor;
        Shell shell;
        bool keepRunning;

        sl::UIdAllocator windowIdAllocator;
        sl::Vector<WindowDescriptor*> windows;
        sl::Vector<sl::UIntRect> damageRects;

        WindowDescriptor* dragWindow = nullptr;
        sl::Vector2i cursorPos;

        WindowManager();

        void HandleKeyEvent(KeyEvent event);
        void HandleMouse(bool pressed, bool primary);

        uint64_t CreateWindow(const np::Gui::CreateWindowRequest* req, const ProtocolClient client);

    public:
        static void Run();

        void ProcessPacket(const ProtocolClient from, sl::BufferView packet);
    };
}
