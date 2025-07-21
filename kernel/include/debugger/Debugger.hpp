#pragma once

#include <Types.h>
#include <Span.h>

namespace Npk::Debugger
{
    enum class EventType
    {
        RequestConnect,
        RequestDisconnect,
        AddTransport,

        Count
    };

    enum class DebugError
    {
        Success = 0,
        InvalidArgument,
        OutOfResources,
        NotSupported,
        BadEnvironment,
    };

    struct DebugTransport
    {
        const char* name;
        void* opaque;

        void (*Send)(DebugTransport* inst, sl::Span<uint8_t> data);
        size_t (*Receive)(DebugTransport* inst, sl::Span<uint8_t> buffer, sl::Span<uint8_t> breakSeq);
    };
    
    struct DebugProtocol
    {
        const char* name;
        DebugTransport* transport;
        void* opaque;

        DebugError (*Connect)(DebugProtocol* inst);
        void (*Disconnect)(DebugProtocol* inst);
    };

    DebugError Initialize(size_t cpuCount);
    DebugError Connect();
    void Disconnect();
    void AddTransport(DebugTransport* transport);
}
