#pragma once

#include <stdint.h>
#include <NativePtr.h>

namespace Kernel::Scheduling
{
    enum class ThreadGroupEventType : uint32_t
    {
        Null = 0,
        ExitGracefully = 1,
        ExitImmediatelly = 2,
        IncomingMail = 3,
        KeyEvent = 4,
        MouseEvent = 5,
    };

    struct ThreadGroupEvent
    {
        ThreadGroupEventType type;
        uint32_t length;
        sl::NativePtr address;

        ThreadGroupEvent(ThreadGroupEventType type, uint32_t len, sl::NativePtr addr)
        : type(type), length(len), address(addr)
        {}
    };
}
