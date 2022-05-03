#pragma once

#include <stdint.h>
#include <stddef.h>
#include <Vectors.h>
#include <WindowDescriptor.h>

namespace WindowServer
{    
    enum class RequestType : uint64_t
    {
        CreateWindow = 0,
        DestroyWindow = 1,
        MoveWindow = 2,
        ResizeWindow = 3,
        InvalidateContents = 4,
    };

    struct BaseRequest
    {
        const RequestType type;

        BaseRequest(RequestType t) : type(t) {}
    };

    struct CreateWindowRequest : public BaseRequest
    {
        sl::Vector2u size;
        sl::Vector2u position;
        uint64_t monitor;
        WindowFlags flags;
        char titleStr[];

        CreateWindowRequest() : BaseRequest(RequestType::CreateWindow) {}
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
