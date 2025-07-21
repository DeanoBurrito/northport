#include <debugger/GdbRemote.hpp>

namespace Npk::Debugger
{
    //foward declarations for use in gdbProtocolInstance
    static DebugError GdbConnect(DebugProtocol* inst);
    static void GdbDisconnect(DebugProtocol* inst);

    constexpr size_t WorkingBufferSize = 256;

    struct GdbProtocol
    {
        DebugProtocol debugProtocol;
        uint8_t workingBuffer[WorkingBufferSize];
    };

    static GdbProtocol gdbProtocolInstance
    {
        .debugProtocol =
        {
            .name = "gdb-remote",
            .opaque = &gdbProtocolInstance,

            .Connect = GdbConnect,
            .Disconnect = GdbDisconnect,
        },
    };

    DebugProtocol* GetGdbProtocol()
    {
        return &gdbProtocolInstance.debugProtocol;
    }

    static size_t ReceivePacket(DebugTransport* transport, sl::Span<uint8_t> buffer)
    {
        const size_t received = transport->Receive(transport, buffer);

        if (received < 4)
            return 0; //minimum packet size is 4 chars
    }
    static_assert(sizeof(uint8_t) == sizeof(char));
    
    static void SendPacket(DebugTransport* transport, sl::Span<const uint8_t> buffer)
    {}

    static void SendAck(DebugTransport* transport, bool positive = true)
    {
        const char* str = positive ? "+" : "-";
        sl::Span<const uint8_t> buffer = { reinterpret_cast<const uint8_t*>(str), 1 };

        SendPacket(transport, buffer);
    }
    static_assert(sizeof(uint8_t) == sizeof(char));

    static DebugError GdbConnect(DebugProtocol* inst)
    {
        if (inst == nullptr)
            return DebugError::InvalidArgument;
        if (inst->transport == nullptr)
            return DebugError::InvalidArgument;

        auto& comms = inst->transport;
        auto* gdb = static_cast<GdbProtocol*>(inst->opaque);
        
        while (true)
        {
            //keep waiting for data to come in, until we receive a '?' packet
            const size_t received = ReceivePacket(comms, gdb->workingBuffer);
            if (received == 0)
                continue;
            if (gdb->workingBuffer[1] != '?')
                continue;

            SendAck(comms);
            //host has connected and sent the '?' packet: we should tell it why we stopped
            break;
        }

        return DebugError::Success;
    }

    static void GdbDisconnect(DebugProtocol* inst)
    {
    }
}
