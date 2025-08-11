#include <debugger/ProtocolGdb.hpp>
#include <NanoPrintf.hpp>

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
            .transport = nullptr,
            .opaque = &gdbProtocolInstance,

            .Connect = GdbConnect,
            .Disconnect = GdbDisconnect,
        },
        .workingBuffer = {},
    };

    DebugProtocol* GetGdbProtocol()
    {
        return &gdbProtocolInstance.debugProtocol;
    }

    static uint8_t DecodeByte(sl::Span<const uint8_t> buffer)
    {
        if (buffer.Empty())
            return 0;

        uint8_t byte = 0;
        if (buffer[0] >= '0' || buffer[0] <= '9')
            byte = buffer[0] - '0';
        else if (buffer[0] >= 'a' || buffer[0] <= 'f')
            byte = buffer[0] - 'a' + 10;
        else if (buffer[0] >= 'A' || buffer[0] <= 'F')
            byte = buffer[0] - 'A' + 10;

        return byte | (DecodeByte(buffer.Subspan(1, -1)) << 4);
    }

    static void EncodeByte(uint8_t byte, sl::Span<uint8_t> buffer)
    {
        if (buffer.Size() < 2)
            return;

        constexpr const char lut[] = "0123456789abcdef";
        buffer[0] = lut[(byte >> 4) & 0xF];
        buffer[1] = lut[byte & 0xF];
    }

    static uint8_t ComputeChecksum(sl::Span<const uint8_t> buffer)
    {
        uint8_t accum = 0;
        for (size_t i = 0; i < buffer.Size(); i++)
            accum += buffer[i];
        return accum;
    }
    
    static bool SendPacket(DebugTransport* transport, sl::Span<const uint8_t> buffer)
    {
        return transport->Send(transport, buffer);
    }

    static size_t FormatPacket(sl::Span<uint8_t> buffer, sl::StringSpan format, ...)
    {
        if (buffer.Size() < 4)
            return 0;
        const size_t formatSpace = buffer.Size() - 4;

        va_list args;
        va_start(args, format);
        const size_t formattedLen = npf_vsnprintf((char*)buffer.Subspan(1, formatSpace).Begin(), 
            formatSpace, format.Begin(), args);
        va_end(args);

        buffer[0] = '$';
        buffer[formattedLen] = '#';

        uint8_t checksum = 0;
        for (size_t i = 0; i < formattedLen; i++)
            checksum += buffer[i + 1];
        EncodeByte(checksum, buffer.Subspan(formattedLen + 2, 2));

        return formattedLen + 4;

    }
    static_assert(sizeof(uint8_t) == sizeof(char));

    static void SendAck(DebugTransport* transport, bool positive = true)
    {
        const char* str = positive ? "+" : "-";
        sl::Span<const uint8_t> buffer = { reinterpret_cast<const uint8_t*>(str), 1 };

        SendPacket(transport, buffer);
    }
    static_assert(sizeof(uint8_t) == sizeof(char));

    static sl::Span<const uint8_t> ReceivePacket(DebugTransport* transport, sl::Span<uint8_t> buffer)
    {
        size_t receiveHead = 0;
        while (true)
        {
            const size_t received = transport->Receive(transport, buffer.Subspan(receiveHead, -1));
            receiveHead += received;

            size_t dataEnd = -1;
            for (size_t i = 0; i < receiveHead; i++)
            {
                //scan buffer for the end of a packet: "#xx", where "xx" are the checksum digits.
                if (buffer[i] != '#')
                    continue;
                if (i + 2 >= receiveHead)
                    continue;

                dataEnd = i;
                break;
            }

            if (receiveHead == buffer.Size())
            {
                //received too much data: TODO: this is a weird failure path, we return nothing?
                return {}; 
            }
            if (dataEnd == (size_t)-1)
            {
                //we dont have a complete packet yet, carry on receiving
                continue;
            }
            if (dataEnd >= receiveHead)
            {
                //bad packet, reset receiving buffer + NACK to host, they should retry transmission
                SendAck(transport, false);
                receiveHead = 0;
                continue;
            }

            //we found the end of the packet, can we find the beginning?
            size_t dataBegin = -1;
            for (size_t i = 0; i < dataEnd; i++)
            {
                if (buffer[i] != '$')
                    continue;
                dataBegin = i + 1;
                break;
            }
            if (dataBegin == (size_t)-1)
            {
                //we didnt catch the start of the packet, NACK the host.
                SendAck(transport, false);
                receiveHead = 0;
                continue;
            }

            const uint8_t ourChecksum = ComputeChecksum(buffer.Subspan(dataBegin, dataEnd - dataBegin).Const());
            const uint8_t packetChecksum = DecodeByte(buffer.Subspan(dataEnd + 1, 2).Const());
            if (ourChecksum != packetChecksum)
            {
                SendAck(transport, false);
                continue;
            }

            //we have a complete packet in the buffer!
            return buffer.Subspan(dataBegin, dataEnd - dataBegin).Const();
        }
    }

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
            const auto received = ReceivePacket(comms, gdb->workingBuffer);
            if (received.Empty())
                continue;
            if (received.Size() != 1 && received[0] != '?')
            {
                SendAck(comms, false);
                continue;
            }

            SendAck(comms);
            //host has connected and sent the '?' packet: we should tell it why we stopped
            break;
        }

        return DebugError::Success;
    }

    static void GdbDisconnect(DebugProtocol* inst)
    {
        (void)inst;
    }
}
