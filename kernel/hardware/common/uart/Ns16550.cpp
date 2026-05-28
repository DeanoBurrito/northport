#include <hardware/common/uart/Ns16550.hpp>
#include <Vm.hpp>
#include <lib/Printf.hpp>
#include <lib/Mmio.hpp>
#include <lib/Maths.hpp>

#ifdef __x86_64__
    #include <hardware/x86_64/PortIo.hpp>
#endif

namespace Npk
{
    namespace Ns16550
    {
        constexpr size_t FormatChunkSize = 128;
        constexpr size_t MaxHeaderLen = 64;

        constexpr const char ResetColourStr[] = "\e[39m";
        constexpr const char FormatStr[] = "%.0s[c%u %7s]%.0s ";
        constexpr const char ColourFormatStr[] = "%s[c%u %7s]%s ";

        enum class Reg
        {
            Data = 0,
            DivisorLow = 0,
            IntrEnable = 1,
            DivisorHigh = 1,
            IntrId = 2,
            LineControl = 3,
            ModemControl = 4,
            LineStatus = 5,
            ModemStatus = 6,
        };

        static bool doColour;
        static bool available;
        static bool isMmio;
        static bool inPanic;
        static uintptr_t address;
        static size_t txFifoLen = 1;
        static char chunkBuff[FormatChunkSize];
        static size_t chunkLen = 0;

        static inline void WriteReg(Reg reg, uint8_t value)
        {
            if (isMmio)
                sl::MmioWrite8(address + (uintptr_t)reg, value);
            else
                Out8((Port)(address + (uintptr_t)reg), value);
        }

        static inline uint8_t ReadReg(Reg reg)
        {
            if (isMmio)
                return sl::MmioRead8(address + (uintptr_t)reg);
            else
                return In8((Port)(address + (uintptr_t)reg));
        }

        static inline void Flush()
        {
            size_t head = 0;
            while (head < chunkLen)
            {
                while ((ReadReg(Reg::LineStatus) & 0x20) == 0)
                    sl::HintSpinloop();

                const size_t count = sl::Min(txFifoLen, chunkLen - head);
                const uintptr_t where = address + (uintptr_t)Reg::Data;

                if (isMmio)
                {
                    for (size_t i = 0; i < count; i++)
                        sl::MmioWrite8(where, chunkBuff[head + i]);
                }
                else
#ifdef __x86_64__
                {
                    Out8String((Port)where, 
                        reinterpret_cast<const uint8_t*>(&chunkBuff[head]), 
                        count);
                }
#else
                    NPK_UNREACHABLE();
#endif
                head += count;
            }

            chunkLen = 0;
        }

        static inline void Append(const char* text, size_t length)
        {
            while (length > 0)
            {
                const size_t space = FormatChunkSize - chunkLen;
                const size_t count = sl::Min(space, length);

                sl::MemCopy(&chunkBuff[chunkLen], text, count);
                chunkLen += count;
                length -= count;
                text += count;

                if (chunkLen == FormatChunkSize)
                    Flush();
            }
        }

        static inline void Reset()
        {
            //Default settings: 115200 baud, 8 data bits, 1 stop bit, 
            //0 parity bits, no break bit
            const uint8_t divisor = sl::Clamp<uint8_t>(
                ReadConfigUint("npk.ns16550.divisor", 1), 1, 255);
            const uint8_t dataBits = sl::Clamp<uint8_t>(
                ReadConfigUint("npk.ns16550.data_bits", 8), 5, 8) - 5;
            const uint8_t stopBits = sl::Clamp<uint8_t>(
                ReadConfigUint("npk.ns16550.stop_bits", 1), 1, 2) - 1;
            const uint8_t breakBits = sl::Clamp<uint8_t>(
                ReadConfigUint("npk.ns16550.break_bits", 0), 0, 1);

            uint8_t parityBits = 0;
            if (auto val = ReadConfigString("npk.ns16550.parity", {});
                !val.Empty())
            {
                if (val == "odd"_span)
                    parityBits = 0b001;
                else if (val == "even"_span)
                    parityBits = 0b011;
                else if (val == "mark"_span)
                    parityBits = 0b101;
                else if (val == "space"_span)
                    parityBits = 0b111;
            }

            //disable interrupts
            WriteReg(Reg::IntrEnable, 0);

            //set divisor
            WriteReg(Reg::LineControl, 0x80);
            WriteReg(Reg::DivisorLow, divisor);
            WriteReg(Reg::DivisorHigh, 0);

            //apply settings to line driver
            const uint8_t settings = (breakBits << 6) | (parityBits << 3)
                | (stopBits << 2) | dataBits;
            WriteReg(Reg::LineControl, settings);

            //detect FIFO size: 64 bits (16750), 16 bytes (16550A), or 1 in all
            //other cases.
            WriteReg(Reg::IntrId, 0xE7);
            const uint8_t iir = ReadReg(Reg::IntrId);
            if ((iir & 0xC0) == 0xC0)
            {
                if (iir & 0x20)
                    txFifoLen = 64;
                else
                {
                    WriteReg(Reg::IntrId, 0xC7);
                    txFifoLen = 16;
                }
            }
            else
            {
                WriteReg(Reg::IntrId, 0x00);
                txFifoLen = 1;
            }

            //enable RTS and DTR pins, leave interrupts disabled
            WriteReg(Reg::ModemControl, 3);

            const char tail[] = { '\n', '\r' };
            Append(tail, 2);
            Flush();
        }

        static inline void Write(LogSinkMessage msg)
        {
            if (!inPanic)
            {
                const auto levelStr = LogLevelStr(msg.level);
                const char* format = doColour ? ColourFormatStr : FormatStr;
                const char* colourStr = [](LogLevel level) -> const char*
                {
                    switch (level)
                    {
                    case LogLevel::Error:   return "\e[31m";
                    case LogLevel::Warning: return "\e[33m";
                    case LogLevel::Info:    return "\e[97m";
                    case LogLevel::Verbose: return "\e[90m";
                    case LogLevel::Trace:   return "\e[37m";
                    case LogLevel::Debug:   return "\e[34m";
                    default: return "";
                    }
                }(msg.level);

                char header[MaxHeaderLen];
                const size_t headerLen = sl::SnPrintf(header, MaxHeaderLen,
                    format, colourStr, msg.cpu, levelStr.Begin(), 
                    ResetColourStr);
                Append(header, sl::Min(headerLen, MaxHeaderLen - 1));
            }

            Append(msg.text.Begin(), msg.text.Size());

            if (!inPanic)
            {
                const char tail[] = { '\n', '\r' };
                Append(tail, 2);
            }

            if (chunkLen != 0)
                Flush();
        }

        static inline void BeginPanic()
        {
            inPanic = true;
        }

        static bool Transmit(DebugTransport* inst, sl::Span<const uint8_t> data)
        {
            (void)inst;

            Append((const char*)data.Begin(), data.Size());
            Flush();

            return true;
        }

        static size_t Receive(DebugTransport* inst, sl::Span<uint8_t> buffer)
        {
            (void)inst;

            for (size_t i = 0; i < buffer.Size(); i++)
            {
                if ((ReadReg(Reg::LineStatus) & 0b1) == 0)
                    return i;

                buffer[i] = ReadReg(Reg::Data);
            }

            return buffer.Size();
        }
    }

    static LogSink ns16550sink
    {
        .listHook = {},
        .Reset = Ns16550::Reset,
        .Write = Ns16550::Write,
        .BeginPanic = Ns16550::BeginPanic,
    };

    static DebugTransport ns16550transport
    {
        .hook = {},
        .priority = 0,
        .name = "ns16550",
        .opaque = nullptr,
        .Transmit = Ns16550::Transmit,
        .Receive = Ns16550::Receive,
    };

    bool InitNs16550(uintptr_t& virtBase, Paddr regsBase, bool regsMmio)
    {
        Ns16550::doColour = ReadConfigUint("npk.ns16550.colour", true);

        if (regsMmio)
        {
            auto status = SetKernelMap(virtBase, regsBase, 
                VmFlag::Write | VmFlag::Mmio);
            if (status != NpkStatus::Success)
                return false;

            Ns16550::address = virtBase;
            Ns16550::isMmio = true;
            virtBase += PageSize();
        }

#ifdef __x86_64__
        else
        {
            Ns16550::address = regsBase;
            Ns16550::isMmio = false;
        }
#else
        else
            return false;
#endif

        Ns16550::Reset();
        Ns16550::available = true;
        Ns16550::inPanic = false;

        return true;
    }

    bool Ns16550Available()
    {
        return Ns16550::available;
    }

    LogSink& Ns16550LogSink()
    {
        return ns16550sink;
    }

    DebugTransport& Ns16550DebugTransport()
    {
        return ns16550transport;
    }
}
