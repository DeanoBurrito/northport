#include <WindowManager.h>
#include <SyscallFunctions.h>
#include <Format.h>
#include <Maths.h>

namespace WindowServer
{
    WindowManager::WindowManager()
    {
        using namespace np::Syscall;
        keepRunning = true;
        auto maybeIpcHandle = CreateMailbox("WindowServer/Incoming", IpcMailboxFlags::UseSharedMemory | IpcStreamFlags::AccessPublic);
        if (!maybeIpcHandle)
        {
            Log("Window server could not start ipc listener, aborting init.", LogLevel::Error);
            return;
        }
        listenerIpcHandle = *maybeIpcHandle;
        damageRects.PushBack({0, 0, compositor.Size().x, compositor.Size().y});

        EnableDeviceEvents(1);
        EnableDeviceEvents(2);
        Log("Window server started, listening at ipc address: \"WindowServer/Incoming\"", LogLevel::Info);
    }

    void WindowManager::ProcessPacket(uint8_t* buffer)
    {
        const BaseRequest* req = sl::NativePtr(buffer).As<const BaseRequest>();
        switch (req->type)
        {
        case RequestType::CreateWindow:
            CreateWindow(static_cast<const CreateWindowRequest*>(req));
            break;
        default:
            np::Syscall::Log("WindowServer received unknown request type, dropping packet.", np::Syscall::LogLevel::Error);
            break;
        }
    }

    void WindowManager::CreateWindow(const CreateWindowRequest* request)
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
        window->controlFlags = request->flags;
        window->size = request->size;
        window->position = request->position;
        window->title = request->titleStr;
        window->statusFlags = WindowStatusFlags::None;

        const string formattedString = sl::FormatToString("New window created: w=%u, h=%u, x=%u, y=%u, flags=0x%x, title=%s", 0, 
            window->size.x, window->size.y,
            window->position.x, window->position.y,
            (size_t)window->controlFlags,
            window->title.C_Str()
            );
        Log(formattedString, np::Syscall::LogLevel::Info);

        damageRects.PushBack(window->BorderRect());

        //send window id to requested mailbox
    }

    // // void WindowManager::DestroyWindow(const DestroyWindowRequest* request, ProtocolControlBlock* control)
    // // {}

    // // void WindowManager::MoveWindow(const MoveWindowRequest* request, ProtocolControlBlock* control)
    // // {}

    // // void WindowManager::ResizeWindow(const ResizeWindowRequest* request, ProtocolControlBlock* control)
    // // {}

    void WindowManager::Run()
    {
        WindowManager wm;
        while (wm.keepRunning)
        {
            //SleepUntilEvent(0); TODO:
            
            using namespace np::Syscall;
            auto maybeProgEvent = PeekNextEvent();
            
            if (!maybeProgEvent)
                continue;
            
            switch (maybeProgEvent->type)
            {
            case ProgramEventType::ExitGracefully:
                Log("WindowServer requested to exit, doing as requested ...", LogLevel::Warning);
                return;

            case ProgramEventType::IncomingMail:
                {
                    uint8_t buffer[maybeProgEvent->dataLength];
                    ConsumeNextEvent({ buffer, maybeProgEvent->dataLength });
                    wm.ProcessPacket(buffer);
                    break;
                }

            case ProgramEventType::KeyboardEvent:
                Log("Got keyboard input", LogLevel::Info);
                ConsumeNextEvent({});
                break;

            case ProgramEventType::MouseEvent:
            {
                sl::Vector2i cursorOffset;
                ConsumeNextEvent({ &cursorOffset, sizeof(cursorOffset)});

                wm.damageRects.PushBack({ (unsigned long)wm.cursorPos.x, (unsigned long)wm.cursorPos.y, wm.compositor.CursorSize().x, wm.compositor.CursorSize().y });
                wm.cursorPos.x = sl::clamp<long>(wm.cursorPos.x + cursorOffset.x, 0, wm.compositor.Size().x);
                wm.cursorPos.y = sl::clamp<long>(wm.cursorPos.y -cursorOffset.y, 0, wm.compositor.Size().y);
                wm.damageRects.PushBack({ (unsigned long)wm.cursorPos.x, (unsigned long)wm.cursorPos.y, wm.compositor.CursorSize().x, wm.compositor.CursorSize().y });

                break;
            }

            default:
                ConsumeNextEvent({}); //just consume the event, its not something we care about.
                break;
            }

            if (!wm.damageRects.Empty())
            {
                wm.compositor.Redraw(wm.damageRects, wm.windows, { (unsigned long)wm.cursorPos.x, (unsigned long)wm.cursorPos.y });
                wm.damageRects.Clear();
            }
        }
    }
}
