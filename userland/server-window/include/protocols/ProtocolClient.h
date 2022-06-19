#pragma once

#include <stddef.h>

namespace WindowServer
{
    enum class ProtocolType
    {
        Ipc = 0,
    };

    struct ProtocolClient
    {
        ProtocolType protocol;
        size_t clientId;

        ProtocolClient(ProtocolType type, size_t id) : protocol(type), clientId(id)
        {}
    };
}
