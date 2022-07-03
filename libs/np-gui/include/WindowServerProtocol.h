#pragma once

#include <stddef.h>
#include <stdint.h>

namespace np::Gui
{
    /*
        This header is used by np-gui and the window server (assuming both are compiled with the same version).
        Yes the names of these structs are long, but they're designed for internal use.
        The long names reduce the risk of collision and free up the shorter names for user-facing
        ideas. If you're writing your own window server/gui library, you must deal with these I'm afraid.
    */
    
    constexpr static const char* WindowServerMailbox = "window-server/incoming";
    constexpr static size_t NameLengthLimit = 0x40;

    constexpr static size_t WindowMinWidth = 80;
    constexpr static size_t WindowMinHeight = 40;
    
    enum class RequestType : uint64_t
    {
        NewClient = 0,
        RemoveClient = 1,

        CreateWindow = 2,
        DestroyWindow = 3,
    };

    struct RequestBase
    {
        const RequestType type;

        RequestBase(RequestType t) : type(t)
        {}
    };
    
    struct NewClientRequest : public RequestBase
    {
        uint8_t responseAddr[NameLengthLimit];

        NewClientRequest() : RequestBase(RequestType::NewClient)
        {}
    };

    struct RemoveClientRequest : public RequestBase
    {
        RemoveClientRequest() : RequestBase(RequestType::RemoveClient)
        {}
    };

    struct CreateWindowRequest : public RequestBase
    {
        //-1 for any of these values will be treated as 'dealers choice'.
        uint64_t monitorIndex;
        uint64_t posX;
        uint64_t posY;
        uint64_t width;
        uint64_t height;
        uint8_t title[NameLengthLimit];
        
        CreateWindowRequest() : RequestBase(RequestType::CreateWindow)
        {}
    };

    struct DestroyWindowRequest : public RequestBase
    {
        uint64_t windowId;
        DestroyWindowRequest() : RequestBase(RequestType::DestroyWindow)
        {}
    };

    enum class ResponseType : uint64_t
    {
        GeneralAcknowledge = 0,
        GeneralError = 1,
        AcknowledgeValue = 2,
    };

    struct ResponseBase
    {
        const ResponseType type;

        ResponseBase(ResponseType t) : type(t)
        {}
    };

    struct GeneralAcknowledgeResponse : public ResponseBase
    {
        GeneralAcknowledgeResponse() : ResponseBase(ResponseType::GeneralAcknowledge)
        {}
    };

    struct GeneralErrorResponse : public ResponseBase
    {
        uint64_t value; //TODO: looks a nice place for an enum :^)

        GeneralErrorResponse() : ResponseBase(ResponseType::GeneralError)
        {}
    };

    struct AcknowledgeValueResponse : public ResponseBase
    {
        uint64_t value;

        AcknowledgeValueResponse() : ResponseBase(ResponseType::AcknowledgeValue)
        {}
    };
}
