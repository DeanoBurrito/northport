#include <SyscallFunctions.h>
#include <Logging.h>
#include <protocols/IpcProtocol.h>
#include <WindowManager.h>
#include <Format.h>
#include <Maths.h>

namespace WindowServer
{
    WindowManager::WindowManager()
    {
        keepRunning = true;
        ipcProtocol = new IpcProtocol();
        GenericProtocol::RegisterProto(ipcProtocol);

        //enable event forwarding from keyboard + mouse devices
        auto maybeMouseId = np::Syscall::GetAggregateId(np::Syscall::DeviceType::Mouse);
        if (!maybeMouseId)
            Log("Window server could not get aggregate mouse device, no mouse input will be available for np-gui driven apps.", LogLevel::Error);
        else
            np::Syscall::EnableDeviceEvents(*maybeMouseId);

        auto maybeKeyboardId = np::Syscall::GetAggregateId(np::Syscall::DeviceType::Keyboard);
        if (!maybeKeyboardId)
            Log("Window server could not get aggregate keyboard device, no keyboard input will be available for np-gui driven apps.", LogLevel::Error);
        else
            np::Syscall::EnableDeviceEvents(*maybeKeyboardId);

        np::Userland::Log("Window server started, %u protocols in use.", LogLevel::Info, 1);
    }

    extern WindowManager* globalWindowManager; //defined in GenericProtocol.cpp
    void WindowManager::Run()
    {
        WindowManager wm;
        globalWindowManager = &wm;

        //redraw the entire screen by creating a giant damage rect all of it.
        wm.damageRects.PushBack({ 0, 0, wm.compositor.Size().x, wm.compositor.Size().y });
        wm.compositor.Redraw(wm.damageRects, wm.windows, { 0, 0 });
        wm.damageRects.Clear();

        /*
            Main loop is event based. We only run in response to an external event. Usually this is an IPC message,
            but it may expand to other things in future. The `Sleep(0, true)` call is the core part.
            It will sleep the main thread indefinitely until a program event is pushed by the kernel,
            at which point we awake, process it, optionally redraw the screen, and then sleep again.
        */
        while (wm.keepRunning)
        {
            np::Syscall::Sleep(0, true);
            
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
                    if (maybeProgEvent->handle == wm.ipcProtocol->GetIpcHandle())
                        wm.ipcProtocol->HandlePendingEvents();
                    else
                        Log("Window server received unsolicited IPC message.", LogLevel::Error);
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

    void WindowManager::ProcessPacket(const ProtocolClient from, sl::BufferView packet)
    {
        np::Syscall::Log("Window server got packet!", LogLevel::Info);
    }

    void WindowManager::ProcessNewClient(const ProtocolClient client, const np::Gui::NewClientRequest* request)
    {}
}
