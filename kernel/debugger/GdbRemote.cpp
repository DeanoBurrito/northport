#include <DebuggerPrivate.hpp>
#include <Memory.hpp>

#include <Core.hpp>
#include <Maths.hpp>

namespace Npk::Private
{
    constexpr size_t BuiltinBufferSize = 512;

    struct GdbData
    {
        DebugTransport* transport;
        uint8_t builtinRecvBuff[BuiltinBufferSize];
        uint8_t builtinSendBuff[BuiltinBufferSize];
    } gdbData;

    static bool IsHexDigit(uint8_t test)
    {
        return (test >= '0' && test <= '9') 
            || (test >= 'a' && test <= 'f')
            || (test >= 'A' && test <= 'F');
    }

    static uint8_t FromHex(uint8_t input)
    {
        if (input >= '0' && input <= '9')
            return input - '0';
        if (input >= 'a' && input <= 'f')
            return input - 'a' + 10;
        if (input >= 'A' && input <= 'F')
            return input - 'A' + 10;
        
        return 0;
    }

    static uint8_t ToHex(uint8_t input)
    {
        constexpr char digits[] = "0123456789abcdef";

        return digits[input] & 0xF;
    }

    static uint8_t ComputeChecksum(sl::Span<uint8_t> buffer)
    {
        uint8_t sum = 0;

        //the start/end of packet markers arent included in the checksum
        if (buffer.Size() > 1 && buffer[0] == '$')
            buffer = buffer.Subspan(1, -1);
        if (buffer.Size() > 1 && *(buffer.End() - 1) == '#')
            buffer = buffer.Subspan(0, buffer.Size() - 1);

        for (size_t i = 0; i < buffer.Size(); i++)
            sum += buffer[i];

        return sum;
    }

    static size_t PutBytes(sl::Span<uint8_t> buffer, void* data, size_t dataLen)
    {
        const uint8_t* store = static_cast<uint8_t*>(data);

        size_t bytesWritten = 0;
        while (bytesWritten < dataLen && bytesWritten * 2 + 1 < buffer.Size())
        {
            const uint8_t high = ToHex(store[bytesWritten] >> 4);
            const uint8_t low = ToHex(store[bytesWritten] & 0xF);

            buffer[bytesWritten * 2] = high;
            buffer[bytesWritten * 2 + 1] = low;
            bytesWritten++;
        }

        return bytesWritten * 2;
    }

    static size_t GetBytes(sl::Span<uint8_t> buffer, void* data, size_t dataLen)
    {
        //TODO: need to look into possible endianness issues here
        uint8_t* store = static_cast<uint8_t*>(data);

        size_t bytesRead = 0;
        while (bytesRead < dataLen && bytesRead * 2 + 1 < buffer.Size())
        {
            const uint8_t high = FromHex(buffer[bytesRead * 2]);
            const uint8_t low = FromHex(buffer[bytesRead * 2 + 1]);
            const uint8_t full = (high << 4) | low;

            store[bytesRead] = full;
            bytesRead++;
        }

        return bytesRead * 2;
    }

    static bool Send(GdbData& inst, sl::Span<uint8_t> buffer)
    {
    }

    static size_t Receive(GdbData& inst, sl::Span<uint8_t> buffer)
    {
        size_t receivedLen = 0;
        bool hasStart = false;
        bool hasEnd = false;
        bool sendAck = true;

        while (true)
        {
            auto localBuffer = buffer.Subspan(receivedLen, -1);
            size_t localLen = inst.transport->Receive(inst.transport,
                localBuffer);

            for (size_t i = 0; i < localLen; i += 80)
                Log("gdbrev: %.*s", LogLevel::Debug, sl::Min<int>(localLen - i, 80), &localBuffer[i]);

            if (!hasStart)
            {
                //try to find the start of the packet.
                for (size_t i = 0; i < localLen; i++)
                {
                    if (localBuffer[i] != '$')
                        continue;

                    //probably a waste of cycles, but move the start of the
                    //packet to the start of the buffer.
                    localLen -= i;
                    sl::MemCopy(buffer.Begin(), &localBuffer[i], localLen);
                    hasStart = true;
                    receivedLen = localLen;
                    break;
                }

                if (!hasStart)
                    continue;
            }
            else
                receivedLen += localLen;

            if (!hasEnd)
            {
                //find the end of the packet if we can.
                for (size_t i = 0; i < receivedLen; i++)
                {
                    if (buffer[i] != '#')
                        continue;
                    if (i + 2 >= receivedLen)
                        continue;

                    receivedLen = i + 3; //+2 for checksum, +1 for '#'
                    hasEnd = true;
                    break;
                }

                if (hasEnd)
                    break;
            }
        }

        uint8_t checksum;
        GetBytes(buffer.Subspan(receivedLen - 2, -1), &checksum, 1);
        if (checksum != ComputeChecksum(buffer.Subspan(0, receivedLen - 2)))
            sendAck = false;

        //let the host know we received its data via an ack/nack
        const uint8_t ack = sendAck ? '+' : '-';
        inst.transport->Transmit(inst.transport, { &ack, 1 });
        //TODO: error handling for Transmit()

        return receivedLen;
    }

    static DebugStatus GdbConnect(DebugProtocol* inst, DebugTransportList* ports)
    {
        for (auto it = ports->Begin(); it != ports->End(); ++it)
        {
            gdbData.transport = &*it;

            if (Receive(gdbData, gdbData.builtinRecvBuff) != 0)
                break;
            gdbData.transport = nullptr;
        }

        if (gdbData.transport == nullptr)
            return DebugStatus::BadEnvironment;
        return DebugStatus::Success;
    }

    static void GdbDisconnect(DebugProtocol* inst)
    {
        SL_UNREACHABLE();
    }

    DebugProtocol gdbProtocol
    {
        .name = "gdb-remote",
        .opaque = &gdbData,
        .Connect = GdbConnect,
        .Disconnect = GdbDisconnect,
    };
}
