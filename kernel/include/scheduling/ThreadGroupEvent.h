#pragma once

#include <stdint.h>

namespace Kernel::Scheduling
{
    enum class ThreadGroupEventType : uint32_t
    {
        Null = 0,
        ExitGracefully = 1,
        ExitImmediatelly = 2,
        IncomingMail = 3,
    };

    struct ThreadGroupEvent
    {
        ThreadGroupEventType type;
        uint32_t length;
        uint64_t address;

        ThreadGroupEvent(ThreadGroupEventType type, uint32_t len, uint64_t addr)
        : type(type), length(len), address(addr)
        {}
    };
}
