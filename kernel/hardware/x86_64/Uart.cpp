#include <hardware/x86_64/Private.hpp>
#include <hardware/x86_64/PortIo.hpp>
#include <hardware/common/Ns16550.hpp>
#include <NanoPrintf.hpp>

namespace Npk
{
    constexpr const char ResetColourStr[] = "\e[39m";
    constexpr const char FormatStr[] = "%.0s[c%u %7s]%.0s ";
    constexpr const char ColourFormatStr[] = "%s[c%u %7s]%s ";
    constexpr Paddr Com1RegBase = 0x3F8;

    static bool debugconDoColour;
    static bool inPanic;

    static void DebugconPutc(int c, void* ignored)
    {
        (void)ignored;
        Out8(Port::Debugcon, static_cast<uint8_t>(c));
    }

    static void DebugconWrite(LogSinkMessage msg)
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

        if (!inPanic)
        {
            npf_pprintf(DebugconPutc, nullptr, format, colourStr, msg.cpu, 
                levelStr.Begin(), ResetColourStr);
        }

        for (size_t i = 0; i < msg.text.Size(); i++)
            Out8(Port::Debugcon, msg.text[i]);

        if (!inPanic)
        {
            Out8(Port::Debugcon, '\n');
            Out8(Port::Debugcon, '\r');
        }
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
        if (ReadConfigUint("npk.x86.debugcon_enable", true))
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
