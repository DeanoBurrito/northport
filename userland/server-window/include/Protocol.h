#pragma once

#include <stdint.h>
#include <stddef.h>
#include <Vectors.h>
#include <WindowDescriptor.h>

namespace WindowServer
{    
    enum class RequestType : uint64_t
    {
        NotReady = 0,
        CreateWindow = 1,
        DestroyWindow = 2,
        MoveWindow = 3,
        ResizeWindow = 4,
        InvalidateContents = 5,
    };

    enum class ResponseType : uint64_t
    {
        NotReady = 0,
        RequestNotSupported = 1,
        Ready = 2,
    };

    struct ProtocolControlBlock
    {
        RequestType requestType;
        ResponseType responseType;
    };

    struct CreateWindowRequest
    {
        sl::Vector2u size;
        sl::Vector2u position;
        uint64_t monitor;
        WindowFlags flags;
        char titleStr[];
    };

    struct DestroyWindowRequest
    {
        uint64_t windowId;
    };

    struct MoveWindowRequest
    {
        uint64_t windowId;
        sl::Vector2u newPosition;
    };

    struct ResizeWindowRequest
    {
        uint64_t windowId;
        sl::Vector2u newSize;
    };

    //invalidate contents has no struct, as it takes no args
}
