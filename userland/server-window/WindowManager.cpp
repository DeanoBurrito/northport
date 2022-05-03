#include <WindowManager.h>
#include <SyscallFunctions.h>
#include <Format.h>

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
            //SleepUntilEvent(0);
            
            using namespace np::Syscall;
            auto maybeProgEvent = PeekNextEvent();
            
            //TODO: would be nice to sleep on events, something like a high-level mwait
            //NOTE: we are using mailboxes now, so we'll be using SleepUntilEvents(0)
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
                    ProgramEvent ev = *ConsumeNextEvent({ buffer, maybeProgEvent->dataLength });
                    wm.ProcessPacket(buffer);

                    break;
                }

            default:
                ConsumeNextEvent({}); //just consume the event, its not something we care about.
                break;
            }

            wm.compositor.DrawAll(wm.windows);
        }
    }
}
