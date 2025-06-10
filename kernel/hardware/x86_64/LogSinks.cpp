#include <hardware/x86_64/PortIo.hpp>
#include <hardware/x86_64/Cpuid.hpp>
#include <KernelApi.hpp>
#include <NanoPrintf.h>
#include <Maths.h>

namespace Npk
{
    bool debugconDoColour;

    static void DebugconPutc(int c, void* ignored)
    {
        (void)ignored;
        Out8(Port::Debugcon, static_cast<uint8_t>(c));
    }

    static void DebugconWrite(LogSinkMessage msg)
    {
        constexpr const char FormatStr[] = "%.0s[c%u %7s]%.0s ";
        constexpr const char ColourFormatStr[] = "%s[c%u %7s]%s ";
        constexpr const char ResetColourStr[] = "\e[39m";

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
            }
        }(msg.level);

        npf_pprintf(DebugconPutc, nullptr, format, colourStr, msg.cpu, levelStr.Begin(), ResetColourStr);

        for (size_t i = 0; i < msg.text.Size(); i++)
            Out8(Port::Debugcon, msg.text[i]);

        Out8(Port::Debugcon, '\n');
        Out8(Port::Debugcon, '\r');
    }

    static void DebugconReset()
    {
        Out8(Port::Debugcon, '\n');
        Out8(Port::Debugcon, '\r');
    }

    LogSink debugconSink
    {
        .listHook = {},
        .Reset = DebugconReset,
        .Write = DebugconWrite
    };

    bool CheckForDebugcon()
    {
        debugconDoColour = ReadConfigUint("npk.x86.debugcon_do_colour", true);

        //reading from port 0xE9 returns 0xE9 on qemu and bochs, can be used to detect
        //the presence of debugcon.
        if (In8(Port::Debugcon) != 0xE9)
            return false;

        AddLogSink(debugconSink);
        return true;
    }

    enum class UartReg
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

    bool com1DoColour;

    static inline void WriteUartReg(UartReg reg, uint8_t value)
    {
        Out8(static_cast<Port>((uint16_t)reg + (uint16_t)Port::Com1), value);
    }

    static inline uint8_t ReadUartReg(UartReg reg)
    {
        return In8(static_cast<Port>((uint16_t)reg + (uint16_t)Port::Com1));
    }

    static void Com1Putc(int c, void* ignored)
    {
        (void)ignored;

        while ((ReadUartReg(UartReg::LineStatus) & 0x20) == 0)
            sl::HintSpinloop();

        WriteUartReg(UartReg::Data, static_cast<uint8_t>(c));
    }

    static void Com1Write(LogSinkMessage msg)
    {
        constexpr const char FormatStr[] = "%.0s[c%u %7s]%.0s ";
        constexpr const char ColourFormatStr[] = "%s[c%u %7s]%s ";
        constexpr const char ResetColourStr[] = "\e[39m";

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
            }
        }(msg.level);

        npf_pprintf(Com1Putc, nullptr, format, colourStr, msg.cpu, levelStr.Begin(), ResetColourStr);

        for (size_t i = 0; i < msg.text.Size(); i++)
            Com1Putc(msg.text[i], nullptr);

        Com1Putc('\n', nullptr);
        Com1Putc('\r', nullptr);
    }

    static void Com1Reset()
    {
        //Default settings: 115200 baud, 8 data bits, 1 stop bit, 0 parity bits, no break bit
        const uint8_t divisor = sl::Clamp<uint8_t>(ReadConfigUint("npk.x86.com1_divisor", 1), 1, 255);
        const uint8_t dataBits = sl::Clamp<uint8_t>(ReadConfigUint("npk.x86.com1_data_bits", 8), 5, 8) - 5;
        const uint8_t stopBits = sl::Clamp<uint8_t>(ReadConfigUint("npk.x86.com1_stop_bits", 1), 1, 2) - 1;
        const uint8_t breakBits = sl::Clamp<uint8_t>(ReadConfigUint("npk.x86.com1_break_bits", 0), 0, 1);

        uint8_t parityBits = 0;
        if (auto value = ReadConfigString("npk.x86.com1_parity", {}); !value.Empty())
        {
            if (value == "odd"_span)
                parityBits = 0b001;
            else if (value == "even"_span)
                parityBits = 0b011;
            else if (value == "mark"_span)
                parityBits = 0b101;
            else if (value == "space"_span)
                parityBits = 0b111;
        }

        //disable interrupts
        WriteUartReg(UartReg::IntrEnable, 0);

        //set divisor
        WriteUartReg(UartReg::LineControl, 0x80);
        WriteUartReg(UartReg::DivisorLow, divisor);
        WriteUartReg(UartReg::DivisorHigh, 0);
        const uint8_t lineSettings = (breakBits << 6) | (parityBits << 3) | (stopBits << 2) | dataBits;
        WriteUartReg(UartReg::LineControl, lineSettings);

        WriteUartReg(UartReg::IntrId, 0xC7); //enable FIFO, max threshoold (14 bytes)
        WriteUartReg(UartReg::ModemControl, 3); //enable RTS and DTR pins, leave interrupts disabled

        Com1Putc('\n', nullptr);
        Com1Putc('\r', nullptr);
    }

    LogSink com1Sink
    {
        .listHook = {},
        .Reset = Com1Reset,
        .Write = Com1Write
    };

    bool CheckForCom1()
    {
        com1DoColour = ReadConfigUint("npk.x86.com1_do_colour", true);
        AddLogSink(com1Sink);

        return false;
    }
}
