#pragma once

#include <Types.hpp>
#include <Span.hpp>

namespace Npk::Debugger
{
    enum class EventType
    {
        RequestConnect,
        RequestDisconnect,
        AddTransport,

        CpuException,
        Interrupt,
        Ipi,

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

        //returns whether sending was error-free
        bool (*Send)(DebugTransport* inst, sl::Span<const uint8_t> data);
        //returns number of bytes received
        size_t (*Receive)(DebugTransport* inst, sl::Span<uint8_t> buffer);
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
    void NotifyOfEvent(EventType type, void* eventData);

    void AddTransport(DebugTransport* transport);
}
