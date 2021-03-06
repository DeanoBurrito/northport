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
        windowIdAllocator.Alloc();
        ipcProtocol = new IpcProtocol();
        GenericProtocol::RegisterProto(ipcProtocol);

        //enable event forwarding from keyboard + mouse devices
        auto maybeMouseId = np::Syscall::GetAggregateId(np::Syscall::DeviceType::Mouse);
        if (!maybeMouseId)
            Log("Window server could not get aggregate mouse device, no mouse input will be available for np-gui driven apps.", LogLevel::Error);
        else
            np::Syscall::DeviceEventControl(*maybeMouseId, true);

        auto maybeKeyboardId = np::Syscall::GetAggregateId(np::Syscall::DeviceType::Keyboard);
        if (!maybeKeyboardId)
            Log("Window server could not get aggregate keyboard device, no keyboard input will be available for np-gui driven apps.", LogLevel::Error);
        else
            np::Syscall::DeviceEventControl(*maybeKeyboardId, true);

        np::Userland::Log("Window server started, %u protocols in use.", LogLevel::Info, 1);
    }

    uint64_t WindowManager::CreateWindow(const np::Gui::CreateWindowRequest* req, const ProtocolClient client)
    {
        const uint64_t id = windowIdAllocator.Alloc();

        while (id >= windows.Size())
            windows.EmplaceBack();
        windows[id] = new WindowDescriptor(client);
        WindowDescriptor* desc = windows[id];

        desc->monitorIndex = 0;
        desc->controlFlags = WindowControlFlags::ShowTitlebar;
        desc->statusFlags = WindowStatusFlags::NeedsRedraw;
        desc->windowId = id;

        if (req->width == -1ul || req->height == -1ul)
            desc->size = { np::Gui::WindowMinWidth, np::Gui::WindowMinHeight }; //TODO: smarter default sizing and placement
        else
            desc->size = { sl::max(req->width, np::Gui::WindowMinWidth), sl::max(req->height, np::Gui::WindowMinHeight) };
        
        if (req->posX == -1ul || req->posY == -1ul)
            desc->position = { 20, 20 };
        else
            desc->position = { req->posX, req->posY }; //TODO: make sure we can't spawn windows *outside* of the screen.

        const size_t titleLength = sl::memfirst(req->title, 0, np::Gui::NameLengthLimit);
        char* titleBuffer = new char[titleLength == (size_t)-1 ? np::Gui::NameLengthLimit + 1 : titleLength + 1];
        sl::memcopy(req->title, titleBuffer, titleLength == (size_t)-1 ? np::Gui::NameLengthLimit : titleLength + 1);
        desc->title = sl::String(titleBuffer, true);

        np::Userland::Log("Created window: mon=%u, x=%u, y=%u, w=%u, h=%u, title=%s", LogLevel::Verbose, 
            desc->monitorIndex, desc->position.x, desc->position.y, desc->size.x, desc->size.y, desc->title.C_Str());

        //create a framebuffer for this window, and push a damage rect that covers it so it's immediately redrawn
        compositor.AttachFramebuffer(desc);
        damageRects.PushBack(desc->Rect());

        return id;
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
                ConsumeNextEvent({ &cursorOffset, sizeof(cursorOffset) });

                wm.damageRects.PushBack({ (unsigned long)wm.cursorPos.x, (unsigned long)wm.cursorPos.y, 
                    wm.compositor.CursorSize().x, wm.compositor.CursorSize().y });
                
                wm.cursorPos.x = sl::clamp<long>(wm.cursorPos.x + cursorOffset.x, 0, (long)wm.compositor.Size().x);
                wm.cursorPos.y = sl::clamp<long>(wm.cursorPos.y -cursorOffset.y, 0, (long)wm.compositor.Size().y);

                wm.damageRects.PushBack({ (unsigned long)wm.cursorPos.x, (unsigned long)wm.cursorPos.y, 
                    wm.compositor.CursorSize().x, wm.compositor.CursorSize().y });

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
        using namespace np::Gui;

        const RequestBase* baseReq = packet.base.As<RequestBase>();
        switch (baseReq->type)
        {
        case RequestType::NewClient:
        {
            GeneralAcknowledgeResponse ack;
            GenericProtocol::Send(from, { &ack, sizeof(GeneralAcknowledgeResponse) });
            break;
        }
        
        case RequestType::RemoveClient:
            np::Userland::Log("Removing client %lu:%lu, as per it's request.", LogLevel::Verbose, from.protocol, from.clientId);
            GenericProtocol::Remove(from);
            break;
        
        case RequestType::CreateWindow:
        {
            AcknowledgeValueResponse ack;
            ack.value = CreateWindow(static_cast<const CreateWindowRequest*>(baseReq), from);
            GenericProtocol::Send(from, { &ack, sizeof(AcknowledgeValueResponse) });
            break;
        }

        default:
        {
            GeneralErrorResponse error;
            GenericProtocol::Send(from, { &error, sizeof(GeneralErrorResponse) });
            np::Userland::Log("Unknown request type %lu, from client %lu:%lu", LogLevel::Debug, baseReq->type, from.protocol, from.clientId);
            break;
        }
        }
    }
}
