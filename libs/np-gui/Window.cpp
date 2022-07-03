#include <Window.h>
#include <WindowServerClient.h>
#include <WindowServerProtocol.h>
#include <SyscallFunctions.h>
#include <Locks.h>

namespace np::Gui
{
    Window::Window(WindowServerClient* owner)
    { 
        client = owner;

        CreateWindowRequest req;
        req.monitorIndex = req.posX = req.posY = req.width = req.height = -1;
        req.title[0] = 0;
        client->SendRequest({ &req, sizeof(CreateWindowRequest) }, this);

        sl::SpinlockRelease(&lock);
    }

    Window::Window(WindowServerClient* owner, sl::Vector2u nSize, const sl::String& nTitle)
    {
        client = owner;
        size = nSize;
        title = nTitle;
        
        CreateWindowRequest req;
        req.monitorIndex = req.posX = req.posY = -1;
        req.width = size.x;
        req.height = size.y;
        sl::memcopy(title.C_Str(), req.title, title.Size() + 1);
        client->SendRequest({ &req, sizeof(CreateWindowRequest) }, this);

        sl::SpinlockRelease(&lock);
    }

    Window::Window(WindowServerClient* owner, sl::Vector2u nPos, sl::Vector2u nSize, const sl::String& nTitle)
    {
        client = owner;
        position = nPos;
        size = nSize;
        title = nTitle;
        
        CreateWindowRequest req;
        req.monitorIndex = -1; 
        req.posX = position.x;
        req.posY = position.y;
        req.width = size.x;
        req.height = size.y;
        sl::memcopy(title.C_Str(), req.title, title.Size() + 1);
        client->SendRequest({ &req, sizeof(CreateWindowRequest) }, this);

        sl::SpinlockRelease(&lock);
    }

    Window::~Window()
    {
        sl::ScopedSpinlock scopeLock(&lock);
        if (id == 0)
            return;
        
        DestroyWindowRequest req;
        req.windowId = id;

        np::Syscall::PostToMailbox(WindowServerMailbox, { &req, sizeof(DestroyWindowRequest) });
    }
    
    Window::Window(Window&& from)
    {}

    Window& Window::operator=(Window&& from)
    {}
    
    void Window::Resize(sl::Vector2u newSize)
    {

    }

    void Window::MoveTo(sl::Vector2u newPos)
    {

    }

    void Window::Move(sl::Vector2u offset)
    {

    }

    void Window::SetTitle(const sl::String& newTitle)
    {

    }
}
