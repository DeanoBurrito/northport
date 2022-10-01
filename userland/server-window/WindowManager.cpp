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

    void WindowManager::HandleKeyEvent(KeyEvent event)
    {
        constexpr KeyIdentity mousePrimary = KeyIdentity::MouseLeft;
        constexpr KeyIdentity mouseSecondary = KeyIdentity::MouseRight;

        //anything + gui/super/windows key :( is reserved for the shell process.
        if ((uint16_t)event.mods & (uint16_t)KeyModFlags::BothGuisMask)
        {
            Log("Got shell keys", LogLevel::Debug);
            return;
        }
        
        if (event.id == mousePrimary || event.id == mouseSecondary)
        {
            HandleMouse(sl::EnumHasFlag(event.tags, KeyTags::Pressed), event.id == mousePrimary);
            return;
        }
        //not a mouse click or shell command, pass it through to the active window

        //if its a 'mouse' click, is it inside the active window(passthrough), within the control area (drag + control), into another window (refocus), or somewhere else entirely.
        //is this the start of a reserved key combination (super + x), otherwise pass key event through to window.

    }

    void WindowManager::HandleMouse(bool pressed, bool primary)
    {
        const DecorationConfig& decor = compositor.GetDecorConfig();

        //TODO: accelerate this with a quadtree
        for (size_t i = 0; i < windows.Size(); i++)
        {
            if (windows[i] == nullptr)
                continue;

            const ClickResult result = compositor.TestWindowClick(*windows[i], cursorPos);
            if (result == ClickResult::Miss)
                continue;
            
            else if (result == ClickResult::Border && primary)
            {
                dragWindow = pressed ? windows[i] : nullptr;
                np::Userland::Log("Drag: %s", LogLevel::Debug, dragWindow ? "began" : "ended");
                damageRects.PushBack(Renderer::WindowBorderRect(*windows[i], decor));
                return;
            }
        }
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
        desc->statusFlags = WindowStatusFlags::None;
        desc->windowId = id;

        if (req->width == -1ul || req->height == -1ul)
            desc->size = { np::Gui::WindowMinWidth, np::Gui::WindowMinHeight };
        else
            desc->size = { sl::max(req->width, np::Gui::WindowMinWidth), sl::max(req->height, np::Gui::WindowMinHeight) };
        
        desc->position.x = sl::clamp(req->posX, np::Gui::WindowMinWidth, compositor.Size().x - np::Gui::WindowMinWidth);
        desc->position.y = sl::clamp(req->posY, np::Gui::WindowMinHeight, compositor.Size().y - np::Gui::WindowMinHeight);

        const size_t titleLength = sl::memfirst(req->title, 0, np::Gui::NameLengthLimit);
        char* titleBuffer = new char[titleLength == (size_t)-1 ? np::Gui::NameLengthLimit + 1 : titleLength + 1];
        sl::memcopy(req->title, titleBuffer, titleLength == (size_t)-1 ? np::Gui::NameLengthLimit : titleLength + 1);
        desc->title = sl::String(titleBuffer, true);

        np::Userland::Log("Created window: mon=%u, x=%u, y=%u, w=%u, h=%u, title=%s", LogLevel::Verbose, 
            desc->monitorIndex, desc->position.x, desc->position.y, desc->size.x, desc->size.y, desc->title.C_Str());

        //create a framebuffer for this window, and push a damage rect that covers it so it's immediately redrawn
        compositor.AttachFramebuffer(desc);
        damageRects.PushBack(Renderer::WindowBorderRect(*desc, compositor.GetDecorConfig()));

        return id;
    }

    extern WindowManager* globalWindowManager; //defined in GenericProtocol.cpp
    void WindowManager::Run()
    {
        WindowManager wm;
        globalWindowManager = &wm;

        np::Gui::CreateWindowRequest create0;
        create0.height = 100;
        create0.width = 100;
        create0.monitorIndex = 0;
        create0.posX = 200;
        create0.posY = 200;
        sl::memcopy("Hello window", create0.title, 13);
        ProtocolClient dummyClient(ProtocolType::Ipc, -1);
        wm.CreateWindow(&create0, dummyClient);

        create0.height = 50;
        create0.width = 200;
        create0.posX = 230;
        create0.posY = 100;
        wm.CreateWindow(&create0, dummyClient);
        
        //redraw the entire screen by creating a giant damage rect all of it.
        // wm.damageRects.PushBack({ 0, 0, wm.compositor.Size().x, wm.compositor.Size().y });
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
            {
                KeyEvent event;
                ConsumeNextEvent({ &event, sizeof(KeyEvent) });
                wm.HandleKeyEvent(event);
                break;
            }

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

                if (wm.dragWindow != nullptr)
                {
                    wm.damageRects.PushBack(Renderer::WindowBorderRect(*wm.dragWindow, wm.compositor.GetDecorConfig()));
                    wm.dragWindow->position.x = sl::clamp<long>(wm.dragWindow->position.x + cursorOffset.x, 0, (long)wm.compositor.Size().x);
                    wm.dragWindow->position.y = sl::clamp<long>(wm.dragWindow->position.y -cursorOffset.y, 0, (long)wm.compositor.Size().y);
                    wm.damageRects.PushBack(Renderer::WindowBorderRect(*wm.dragWindow, wm.compositor.GetDecorConfig()));
                }
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
            error.code = GeneralError::UnknownRequest;
            GenericProtocol::Send(from, { &error, sizeof(GeneralErrorResponse) });
            np::Userland::Log("Unknown request type %lu, from client %lu:%lu", LogLevel::Debug, baseReq->type, from.protocol, from.clientId);
            break;
        }
        }
    }
}
