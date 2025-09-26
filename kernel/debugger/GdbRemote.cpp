#include <DebuggerPrivate.hpp>
#include <Memory.hpp>
#include <Maths.hpp>
#include <NanoPrintf.hpp>

#include <Core.hpp>

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
    constexpr size_t ErrorBufferSize = 16;
    constexpr CpuId AllThreads = -1;
    constexpr CpuId AnyThread = -2;

    enum ErrorValue
    {
        MissingArg = 1,
        InvalidArg = 2,
        InternalError = 3,
    };

    struct GdbData
    {
        DebugTransport* transport;
        uint8_t builtinRecvBuff[BuiltinBufferSize];
        uint8_t builtinSendBuff[BuiltinBufferSize];
        CpuId selectedThread;
        CpuId threadListHead;
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

    static size_t PutThreadId(sl::Span<uint8_t> buffer, CpuId id)
    {
        id++; //gdb uses 1-based thread ids, we use 0-based

        //thread ids are specifically encoded big-endian, this code
        //should work regardless of the current machine's endianness.
        size_t bytes = 0;
        while (id != 0)
        {
            bytes++;
            id >>= 8;
        }

        if (bytes * 2 >= buffer.Size())
            return 0;

        for (size_t i = 0; i < bytes; i++)
        {
            const uint8_t byte = id >> (8 * (bytes - i - 1)); 
            //TODO: why is this fucked?
            PutBytes(buffer.Subspan(i * 2, -1), &byte, 1);
        }

        return bytes * 2;
    }

    static size_t PutThreadList(GdbData& inst, sl::Span<uint8_t> buffer)
    {
        const CpuId maxId = MySystemDomain().smpControls.Size();
        const size_t maxBytesUsed = sizeof(CpuId) * 2 + 1;

        size_t head = 0;
        if (inst.threadListHead == maxId)
            buffer[head++] = 'l';
        else
        {
            buffer[head++] = 'm';

            bool first = true;
            for (CpuId i = inst.threadListHead; i < maxId; i++)
            {
                if (head + maxBytesUsed >= buffer.Size())
                    break;

                if (!first)
                    buffer[head++] = ',';
                first = false;

                head += PutThreadId(buffer.Subspan(head, -1), i);
                inst.threadListHead++;
            }
        }

        return head;
    }

    static size_t GetBytes(sl::Span<const uint8_t> buffer, void* data, size_t dataLen)
    {
        //TODO: need to look into possible endianness issues here
        uint8_t* store = static_cast<uint8_t*>(data);

        size_t bytesRead = 0;
        while (bytesRead < dataLen && bytesRead * 2 + 1 < buffer.Size()
            && IsHexDigit(buffer[bytesRead * 2]) 
            && IsHexDigit(buffer[bytesRead * 2 + 1]))
        {
            const uint8_t high = FromHex(buffer[bytesRead * 2]);
            const uint8_t low = FromHex(buffer[bytesRead * 2 + 1]);
            const uint8_t full = (high << 4) | low;

            store[bytesRead] = full;
            bytesRead++;
        }

        return bytesRead * 2;
    }

    static size_t GetThreadId(sl::Span<const uint8_t> buffer, CpuId& id)
    {
        if (buffer.Size() >= 2 && buffer[0] == '-' && buffer[1] == '1')
        {
            id = AllThreads;
            return 2;
        }
        else if (buffer.Size() >= 1 && buffer[0] == '0')
        {
            id = AnyThread;
            return 1;
        }

        id = 0;
        size_t bytes = 0;
        for (size_t i = 0; i < sizeof(id); i++)
        {
            uint8_t byte;
            if (0 == GetBytes(buffer.Subspan(i * 2, -1), &byte, 1))
                break;

            id <<= 8;
            id |= byte;
        }

        id--; //gdb uses 1-based thread ids, we use 0-based.

        return bytes * 2;
    }

    static bool Send(GdbData& inst, sl::Span<const uint8_t> buffer)
    {
        if (!inst.transport->Transmit(inst.transport, buffer))
            return false;

        Log("Sending: %.*s", LogLevel::Debug, (int)buffer.Size(), buffer.Begin());
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

        return Send(inst, buffer.Subspan(0, head).Const());
    }

    static bool SendError(GdbData& inst, ErrorValue which)
    {
        uint8_t buffer[ErrorBufferSize];
        const auto value = static_cast<uint8_t>(which);

        const size_t len = npf_snprintf((char*)buffer, ErrorBufferSize, 
            "$E%01.1x%01.1x#", value >> 4, value & 0xF);

        return CheckAndSend(inst, buffer, len);
    }

    static bool SendUnsupported(GdbData& inst)
    {
        return Send(inst, "$#00"_u8span);
    }

    static bool SendOk(GdbData& inst)
    {
        uint8_t buffer[6];
        sl::MemCopy(buffer, "$OK#", 4);

        return CheckAndSend(inst, buffer, 4);
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
        GetBytes(buffer.Subspan(receivedLen - 2, -1).Const(), &checksum, 1);
        if (checksum != ComputeChecksum(buffer.Subspan(0, receivedLen - 2)))
            sendAck = false;

        //let the host know we received its data via an ack/nack
        const uint8_t ack = sendAck ? '+' : '-';
        inst.transport->Transmit(inst.transport, { &ack, 1 });
        //TODO: error handling for Transmit()

        Log("gdbrcv: %.*s", LogLevel::Debug, (int)receivedLen, buffer.Begin());
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
            .text = "qAttached"_span,
            .Execute = [](GdbData& inst, sl::Span<const uint8_t> data) -> bool
            {
                (void)data;

                inst.builtinSendBuff[0] = '$';
                inst.builtinSendBuff[1] = '1'; //1 = gdb attached to an existing
                inst.builtinSendBuff[2] = '#'; //process.

                return CheckAndSend(inst, inst.builtinSendBuff, 3);
            }
        },
        {
            .text = "qC"_span,
            .Execute = [](GdbData& inst, sl::Span<const uint8_t> data) -> bool
            {
                (void)data;

                size_t len = 0;
                inst.builtinSendBuff[len++] = '$';
                inst.builtinSendBuff[len++] = 'Q';
                inst.builtinSendBuff[len++] = 'C';

                len += PutThreadId(
                    { inst.builtinSendBuff + len, BuiltinBufferSize - len },
                    inst.selectedThread);

                inst.builtinSendBuff[len++] = '#';

                return CheckAndSend(inst, inst.builtinSendBuff, len);
            }
        },
        {
            .text = "qfThreadInfo"_span,
            .Execute = [](GdbData& inst, sl::Span<const uint8_t> data) -> bool
            {
                (void)data;

                inst.threadListHead = 0;

                size_t head = 0;
                inst.builtinSendBuff[head++] = '$';

                sl::Span<uint8_t> buff = inst.builtinSendBuff;
                buff = buff.Subspan(head, buff.Size() - head - 3);
                head += PutThreadList(inst, buff);
                inst.builtinSendBuff[head++] = '#';

                return CheckAndSend(inst, inst.builtinSendBuff, head);
            }
        },
        {
            .text = "qsThreadInfo"_span,
            .Execute = [](GdbData& inst, sl::Span<const uint8_t> data) -> bool
            {
                (void)data;

                size_t head = 0;
                inst.builtinSendBuff[head++] = '$';

                sl::Span<uint8_t> buff = inst.builtinSendBuff;
                buff = buff.Subspan(head, buff.Size() - head - 3);
                head += PutThreadList(inst, buff);
                inst.builtinSendBuff[head++] = '#';

                return CheckAndSend(inst, inst.builtinSendBuff, head);
            }
        },
        {
            .text = "vCont?"_span,
            .Execute = [](GdbData& inst, sl::Span<const uint8_t> data) -> bool
            {
                (void)data;

                const size_t len = npf_snprintf((char*)inst.builtinSendBuff,
                    BuiltinBufferSize, "$vCont;c;C;s;S;r#");

                return CheckAndSend(inst, inst.builtinSendBuff, len);
            }
        },
        {
            .text = "?"_span,
            .Execute = [](GdbData& inst, sl::Span<const uint8_t> data) -> bool
            {
                (void)data;

                size_t head = npf_snprintf((char*)inst.builtinSendBuff,
                    BuiltinBufferSize, "$T05thread:");

                head += PutThreadId({ inst.builtinSendBuff + head, 
                    BuiltinBufferSize - head }, MyCoreId());

                inst.builtinSendBuff[head++] = '#';

                return CheckAndSend(inst, inst.builtinSendBuff, head);
            }
        },
        {
            .text = "H"_span,
            .Execute = [](GdbData& inst, sl::Span<const uint8_t> data) -> bool
            {
                if (data.Size() < 3)
                    return SendError(inst, ErrorValue::MissingArg);

                const uint8_t op = data[1];
                if (op != 'g')
                {
                    if (op == 'c')
                        return SendUnsupported(inst);
                    return SendError(inst, ErrorValue::InvalidArg);
                }

                GetThreadId(data.Subspan(2, -1), inst.selectedThread);
                if (inst.selectedThread == AnyThread)
                    inst.selectedThread = MyCoreId();

                return SendOk(inst);
            }
        },
        {
            .text = "Z"_span,
            .Execute = [](GdbData& inst, sl::Span<const uint8_t> data) -> bool
            {
                size_t head = 2;

                const uint8_t type = data[1];
                uintptr_t addr;
                head += GetBytes(data.Subspan(head, -1), &addr, sizeof(addr));
                uint8_t kind;
                head += GetBytes(data.Subspan(head, -1), &kind, 1);

                Breakpoint* bp = AllocBreakpoint();
                if (bp == nullptr)
                    return SendError(inst, ErrorValue::InternalError);

                bp->kind = kind;

                switch (type)
                {
                case 0: //software breakpoint
                    bp->execute = true;
                    bp->hardware = false;
                    break;
                case 1: //hardware breakpoint
                    bp->execute = true;
                    bp->hardware = true;
                    break;
                case 2: //write watchpoint
                    bp->write = true;
                    break;
                case 3: //read watchpoint
                    bp->read = true;
                    break;
                case 4: //access (read | write) watchpoint
                    bp->read = true;
                    bp->write = true;
                    break;
                }

                if (!ArmBreakpoint(*bp))
                {
                    FreeBreakpoint(&bp);
                    return SendError(inst, ErrorValue::InternalError);
                }

                return SendOk(inst);
            },
        },
        {
            .text = "z"_span,
            .Execute = [](GdbData& inst, sl::Span<const uint8_t> data) -> bool
            {
                size_t head = 2;

                const uint8_t type = data[1];
                uintptr_t addr;
                head += GetBytes(data.Subspan(head, -1), &addr, sizeof(addr));
                uint8_t kind;
                head += GetBytes(data.Subspan(head, -1), &kind, 1);
                (void)kind;

                Breakpoint* bp = GetBreakpointByAddr(addr);
                if (bp == nullptr)
                    return SendError(inst, ErrorValue::InvalidArg);

                if (DisarmBreakpoint(*bp))
                    return SendError(inst, ErrorValue::InternalError);
                FreeBreakpoint(&bp);

                return SendOk(inst);
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

        SendUnsupported(inst);
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
        gdbData.selectedThread = AllThreads;

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
        SL_UNREACHABLE(); (void)inst;
    }

    static DebugStatus GdbBreakpointHit(DebugProtocol* inst, Breakpoint* bp)
    {
        //TODO: send stop packet to host, let them know which breakpoint was hit.
        //TODO: SendStopReason(5, swbreak, ...);
        SL_UNREACHABLE();
    }

    DebugProtocol gdbProtocol
    {
        .name = "gdb-remote",
        .opaque = &gdbData,
        .Connect = GdbConnect,
        .Disconnect = GdbDisconnect,
        .BreakpointHit = GdbBreakpointHit,
    };
}
