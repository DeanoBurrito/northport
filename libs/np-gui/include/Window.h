#pragma once

#include <stddef.h>
#include <Vectors.h>
#include <String.h>

namespace np::Gui
{
    class WindowServerClient;

    class Window
    {
    friend WindowServerClient;
    private:
        WindowServerClient* client;
        size_t id = 0;
        size_t monitor;
        sl::Vector2u position;
        sl::Vector2u size;
        sl::String title;
        char lock;

    public:
        Window() = delete;
        Window(WindowServerClient* owner);
        Window(WindowServerClient* owner, sl::Vector2u size, const sl::String& title);
        Window(WindowServerClient* owner, sl::Vector2u pos, sl::Vector2u size, const sl::String& title);

        ~Window();
        Window(const Window& other) = delete;
        Window& operator=(const Window& other) = delete;
        Window(Window&& from);
        Window& operator=(Window&& from);

        [[gnu::always_inline]] inline
        size_t MonitorIndex() const
        { return monitor; }

        [[gnu::always_inline]] inline
        sl::Vector2u Size() const
        { return size; }

        [[gnu::always_inline]] inline
        sl::Vector2u Position() const
        { return position; }

        [[gnu::always_inline]] inline
        const sl::String& Title() const
        { return title; }

        void Resize(sl::Vector2u newSize);
        void MoveTo(sl::Vector2u newPos);
        void Move(sl::Vector2u offset);
        void SetTitle(const sl::String& newTitle);
    };
}
