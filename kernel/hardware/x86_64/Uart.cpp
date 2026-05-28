#include <hardware/x86_64/Private.hpp>
#include <hardware/x86_64/PortIo.hpp>
#include <hardware/common/uart/Ns16550.hpp>
#include <lib/Printf.hpp>
#include <lib/Maths.hpp>

namespace Npk
{
    constexpr size_t FormatChunkSize = 128;
    constexpr size_t MaxHeaderLen = 64;

    constexpr const char ResetColourStr[] = "\e[39m";
    constexpr const char FormatStr[] = "%.0s[c%u %7s]%.0s ";
    constexpr const char ColourFormatStr[] = "%s[c%u %7s]%s ";
    constexpr Paddr Com1RegBase = 0x3F8;

    static bool debugconDoColour;
    static bool inPanic;

    static void DebugconWrite(LogSinkMessage msg)
    {
        char chunkBuff[FormatChunkSize];
        size_t chunkLen = 0;

        auto Flush = [&]() -> void
        {
            Out8String(Port::Debugcon, reinterpret_cast<uint8_t*>(chunkBuff), 
                chunkLen);
            chunkLen = 0;
        };

        auto Append = [&](const char* text, size_t length) -> void
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
        };

        if (!inPanic)
        {
            const auto levelStr = LogLevelStr(msg.level);
            const char* format = debugconDoColour ? ColourFormatStr : FormatStr;
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
                format, colourStr, msg.cpu, levelStr.Begin(), ResetColourStr);
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

    static void DebugconReset()
    {
        Out8(Port::Debugcon, '\n');
        Out8(Port::Debugcon, '\r');
    }

    static void DebugconBeginPanic()
    {
        inPanic = true;
    }

    LogSink debugconSink
    {
        .listHook = {},
        .Reset = DebugconReset,
        .Write = DebugconWrite,
        .BeginPanic = DebugconBeginPanic,
    };

    void InitUarts()
    {
        bool debugconActive = false;
        if (ReadConfigUint("npk.x86.debugcon_enable", false))
        {
            debugconDoColour = ReadConfigUint("npk.x86.debugcon_colour", true);

            if (In8(Port::Debugcon) == 0xE9)
            {
                debugconActive = true;
                AddLogSink(debugconSink);
            }
        }

        uintptr_t dummy;
        const Paddr com1Addr = ReadConfigUint("npk.x86.com1_addr", Com1RegBase);
        if (InitNs16550(dummy, com1Addr, false))
        {
            if (ReadConfigUint("npk.x86.com1_for_debugger", false))
            {
                Ns16550LogSink().Reset();

                auto trans = &Ns16550DebugTransport();
                trans->priority = 100;
                AddDebugTransport(trans);
            }

            if (!debugconActive)
            {
                auto& sink = Ns16550LogSink();
                AddLogSink(sink);
            }
        }
    }
}
