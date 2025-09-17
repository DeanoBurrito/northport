#include <DebuggerPrivate.hpp>
#include <Memory.hpp>
#include <Maths.hpp>
#include <NanoPrintf.hpp>

/* This file implements support for driving the kernel debugger via the GDB
 * remote protocol (sometimes called the GDB serial protocol).
 *
 * Resources used:
 * - sourceware.org/gdb/current/onlinedocs/gdb.html/Remote-Protocol.html
 * - https://github.com/bminor/binutils-gdb/tree/master/gdb/stubs
 * - https://github.com/OBOS-dev/obos/tree/master/src/oboskrnl/arch/x86_64/gdbstub
 * - GDB has an option (`debug remote`) that is very helpful for debugging this
 *   from the GDB-side of things.
 */
namespace Npk::Private
{
    constexpr size_t BuiltinBufferSize = 512;

    struct GdbData
    {
        DebugTransport* transport;
        uint8_t builtinRecvBuff[BuiltinBufferSize];
        uint8_t builtinSendBuff[BuiltinBufferSize];
        bool breakCommandLoop;

        struct
        {
            bool swBreak;
            bool hwBreak;
            bool errorMsg;
        } features;
    } gdbData;

    struct GdbCommand
    {
        sl::StringSpan text;
        bool (*Execute)(GdbData& inst, sl::Span<const uint8_t> data);
    };


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

        return digits[input & 0xF];
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

    static size_t PutBytes(sl::Span<uint8_t> buffer, const void* data, size_t dataLen)
    {
        const uint8_t* store = static_cast<const uint8_t*>(data);

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

    static bool Send(GdbData& inst, sl::Span<const uint8_t> buffer)
    {
        if (!inst.transport->Transmit(inst.transport, buffer))
            return false;

        while (true)
        {
            uint8_t ack[1];
            size_t ackLen = inst.transport->Receive(inst.transport, ack);
            if (ackLen == 0)
                continue;

            return ack[0] == '+';
        }
    }

    static bool CheckAndSend(GdbData& inst, sl::Span<uint8_t> buffer, size_t head)
    {
        auto checksum = ComputeChecksum(buffer.Subspan(0, head));
        auto checkBuffer = buffer.Subspan(head, head + 2);
        head += PutBytes(checkBuffer, &checksum, sizeof(checksum));

        return Send(inst, buffer.Const());
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

    constexpr GdbCommand GdbCommands[] =
    {
        {
            .text = "qSupported"_span,
            .Execute = [](GdbData& inst, sl::Span<const uint8_t> data) -> bool
            {
                inst.features.swBreak = data.Contains("swbreak+"_u8span);
                inst.features.hwBreak = data.Contains("hwbreak+"_u8span);
                inst.features.errorMsg = data.Contains("error-message+"_u8span);

                const size_t len = npf_snprintf((char*)inst.builtinSendBuff,
                    BuiltinBufferSize, "$swbreak+;hwbreak+;error-message+#");

                return CheckAndSend(inst, inst.builtinSendBuff, len);
            }
        },
        {
            .text = "vCont?"_span,
            .Execute = [](GdbData& inst, sl::Span<const uint8_t> data) -> bool
            {
                const size_t len = npf_snprintf((char*)inst.builtinSendBuff,
                    BuiltinBufferSize, "$vCont;c;s;r#");

                return CheckAndSend(inst, inst.builtinSendBuff, len);
            }
        },
    };

    static bool ProcessPacket(GdbData& inst, sl::Span<const uint8_t> packet)
    {
        const size_t commands = sizeof(GdbCommands) / sizeof(GdbCommand);
        for (size_t i = 0; i < commands; i++)
        {
            const auto cmd = GdbCommands[i];
            const size_t length = sl::Min(packet.Size(), cmd.text.Size());

            if (0 != sl::MemCompare(packet.Begin(), cmd.text.Begin(), length))
                continue;

            return cmd.Execute(inst, packet);
        }

        //unsupported command, as per the spec we should send back an empty
        //packet. The host should understand this as 'not supported'.
        Send(inst, "$#00"_u8span);
        return false;
    }

    static void DoCommandLoop(GdbData& inst)
    {
        inst.breakCommandLoop = false;

        while (!inst.breakCommandLoop)
        {
            const auto recvLen = Receive(inst, inst.builtinRecvBuff);
            if (recvLen == 0)
                continue;

            ProcessPacket(inst, { gdbData.builtinRecvBuff + 1, recvLen - 4 });
        }
    }


    static DebugStatus GdbConnect(DebugProtocol* inst, DebugTransportList* ports)
    {
        for (auto it = ports->Begin(); it != ports->End(); ++it)
        {
            gdbData.transport = &*it;

            const auto recvLen = Receive(gdbData, gdbData.builtinRecvBuff);
            if (recvLen == 0)
            {
                gdbData.transport = nullptr;
                continue;
            }

            //there's a debug host on the other end of this transport! It's
            //sent us a command - we should process and respond.
            ProcessPacket(gdbData, { gdbData.builtinRecvBuff + 1, recvLen - 4});
            break;
        }

        if (gdbData.transport == nullptr)
            return DebugStatus::BadEnvironment;

        DoCommandLoop(gdbData);
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
