#include <WindowManager.h>
#include <SyscallFunctions.h>
#include <Format.h>

namespace WindowServer
{
    WindowManager::WindowManager()
    {
        using namespace np::Syscall;
        keepRunning = true;

        size_t streamSize = 0x1000;
        auto maybeIpcHandle = StartIpcStream("WindowServer/Listener", IpcStreamFlags::UseSharedMemory, streamSize, listenerBuffer.raw);
        if (!maybeIpcHandle)
        {
            Log("Window server could not start ipc listener, aborting init.", LogLevel::Error);
            return;
        }

        listenerIpcHandle = *maybeIpcHandle;
        sl::memset(listenerBuffer.ptr, 0, streamSize);
        Log("Window server started, listening at ipc address: \"WindowServer/Listener\"", LogLevel::Info);
    }

    void WindowManager::ProcessPacket()
    {
        ProtocolControlBlock* controlBlock = listenerBuffer.As<ProtocolControlBlock>();
        controlBlock->responseType = ResponseType::NotReady;

        switch (controlBlock->requestType)
        {
        case RequestType::CreateWindow:
            CreateWindow(listenerBuffer.As<CreateWindowRequest>(sizeof(ProtocolControlBlock)), controlBlock);
            break;
        case RequestType::DestroyWindow:
            DestroyWindow(listenerBuffer.As<DestroyWindowRequest>(sizeof(ProtocolControlBlock)), controlBlock);
            break;
        case RequestType::MoveWindow:
            break;
        case RequestType::ResizeWindow:
            break;
        case RequestType::InvalidateContents:
            break;
        default:
            controlBlock->responseType = ResponseType::RequestNotSupported;
        }

        //clear request type so we dont keep processing the same request
        controlBlock->requestType = RequestType::NotReady;
    }

    void WindowManager::CreateWindow(const CreateWindowRequest* request, ProtocolControlBlock* control)
    {
        WindowDescriptor* window = new WindowDescriptor;
        window->windowId = (size_t)-1;

        for (size_t i = 0; i < windows.Size(); i++)
        {
            if (windows[i] == nullptr)
            {
                windows[i] = window;
                window->windowId = i;
                break;
            }
        }

        if (window->windowId == (size_t)-1)
        {
            window->windowId = windows.Size();
            windows.PushBack(window);
        }

        window->monitorIndex = request->monitor;
        window->flags = request->flags;
        window->size = request->size;
        window->position = request->position;
        window->title = request->titleStr;

        const string formattedString = sl::FormatToString("New window created: w=%u, h=%u, x=%u, y=%u, flags=0x%x, title=%s", 0, 
            window->size.x, window->size.y,
            window->position.x, window->position.y,
            (size_t)window->flags,
            window->title.C_Str()
            );
        Log(formattedString, np::Syscall::LogLevel::Info);

        sl::NativePtr response((size_t)request);
        *response.As<size_t>() = window->windowId;
        
        control->responseType = ResponseType::Ready;
    }

    void WindowManager::DestroyWindow(const DestroyWindowRequest* request, ProtocolControlBlock* control)
    {}

    void WindowManager::MoveWindow(const MoveWindowRequest* request, ProtocolControlBlock* control)
    {}

    void WindowManager::ResizeWindow(const ResizeWindowRequest* request, ProtocolControlBlock* control)
    {}

    void WindowManager::Run()
    {
        WindowManager wm;
        while (wm.keepRunning)
        {
            //TODO: would be nice to sleep on events, something like a high-level mwait
            while (sl::MemRead<uint64_t>(wm.listenerBuffer) == 0);
            wm.ProcessPacket();

            // wm.ProcessPacket();
            wm.compositor.DrawAll(wm.windows);
        }
    }
}
