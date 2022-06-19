#pragma once

#include <stddef.h>
#include <stdint.h>

namespace np::Gui
{
    constexpr static const char* WindowServerMailbox = "window-server/incoming";
    constexpr size_t ResponseEndpointNameLength = 0x40;
    
    enum class RequestType : size_t
    {
        NewClient = 0,
        RemoveClient = 1,

        CreateWindow = 2,
    };

    struct RequestBase
    {
        const RequestType type;

        RequestBase(RequestType t) : type(t)
        {}
    };
    
    struct NewClientRequest : public RequestBase
    {
        uint8_t responseAddr[ResponseEndpointNameLength];

        NewClientRequest() : RequestBase(RequestType::NewClient)
        {}
    };

    struct RemoveClientRequest : public RequestBase
    {
        RemoveClientRequest() : RequestBase(RequestType::RemoveClient)
        {}
    };

    struct CreateWindowRequest
    {};

    enum class ResponseType : size_t
    {
        NewClientId = 0,
        GeneralAcknowledge = 1,
        GeneralError = 2,
    };

    struct ResponseBase
    {
        const ResponseType type;

        ResponseBase(ResponseType t) : type(t)
        {}
    };

    struct NewClientIdRespoinse : public ResponseBase
    {
        
    };
}
