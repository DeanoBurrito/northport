#include <debug/LogBackends.h>
#include <debug/Terminal.h>
#include <debug/TerminalImage.h>
#include <arch/Platform.h>
#include <config/DeviceTree.h>
#include <stdint.h>
#include <NativePtr.h>

namespace Npk::Debug
{
#ifdef NP_INCLUDE_TERMINAL_BG
    asm("\
    .section .rodata \n\
    .balign 0x10 \n\
    BackgroundBegin: \n\
        .incbin \"../misc/TerminalBg.qoi\" \n\
    BackgroundEnd: \n \
    .previous \n \
    ");

    extern uint8_t BackgroundBegin[] asm("BackgroundBegin");
    extern uint8_t BackgroundEnd[] asm("BackgroundEnd");

    GTImage termBg;
#endif
    Terminal term;

    enum Ns16550Reg : size_t
    {
        Data = 0,
        IntControl = 1,
        IntStatus = 2,
        LineControl = 3,
        ModemControl = 4,
        LineStatus = 5,
        ModemStatus = 6,
        Scratch = 7,

        DlabLow = 0,
        DlabHigh = 1,
    };

    sl::NativePtr ns16550mmio;

    bool InitTerminal()
    {
#ifdef NP_INCLUDE_TERMINAL_BG
        GTStyle style { DEFAULT_ANSI_COLOURS, DEFAULT_ANSI_BRIGHT_COLOURS, 0x68000000, 0xDDDDDD, 12, 0 };
        OpenImage(termBg, (uintptr_t)BackgroundBegin, (uintptr_t)BackgroundEnd - (uintptr_t)BackgroundBegin);
        GTBackground bg { &termBg, 0 };
#else
        GTStyle style { DEFAULT_ANSI_COLOURS, DEFAULT_ANSI_BRIGHT_COLOURS, 0x0F0404, 0xDDDDDD, 12, 0 };
        GTBackground bg { NULL,  0 };
#endif
        term.Init(style, bg);
        return true; //TODO: terminal resource usage, report failure.
    }

    bool InitNs16550()
    {
#ifdef __x86_64__
    //NS16550 uart is available over port io.
    #define WriteReg(reg, val) Out8(PortSerial + reg, val)    
#else
    #define WriteReg(reg, val) ns16550mmio.Offset(reg).VolatileWrite<uint8_t>(val)

        //See if there's a compatible node in the device tree.
        ns16550mmio = nullptr;
        auto maybeUart = Config::DeviceTree::Global().GetCompatibleNode("ns16550");
        auto maybeUarta = Config::DeviceTree::Global().GetCompatibleNode("ns16550a");
        if (!maybeUart && !maybeUarta)
            return false;
        
        auto maybeRegs = maybeUart ? maybeUart->GetProp("reg") : maybeUarta->GetProp("reg");
        if (!maybeRegs)
            return false;
        
        const size_t regCount = maybeRegs->ReadRegs(maybeUart ? *maybeUart : *maybeUarta, nullptr, nullptr);
        uintptr_t bases[regCount];
        maybeRegs->ReadRegs(maybeUart ? *maybeUart : *maybeUarta, bases, nullptr);
        ns16550mmio = AddHhdm(bases[0]);
#endif
        WriteReg(Ns16550Reg::IntControl, 0);
        WriteReg(Ns16550Reg::LineControl, 0x80);
        WriteReg(Ns16550Reg::DlabLow, 1); //115200 baud
        WriteReg(Ns16550Reg::DlabHigh, 0);
        WriteReg(Ns16550Reg::LineControl, 3); //8 bits, no parity, 1 stop bit.
        WriteReg(Ns16550Reg::IntStatus, 0xC7); //enable and clear FIFOS, interrupt at 14 chars pending.
        WriteReg(Ns16550Reg::ModemControl, 7); //ints disabled, rts/dts set, out1 enabled.
        //TODO: test uart
        return true;
    }

    void WriteTerminal(const char* str, size_t length)
    {
        term.Write(str, length);
    }

    void WriteDebugcon(const char* str, size_t length)
    {
#ifdef __x86_64__
        for (size_t i = 0; i < length; i++)
            Out8(PortDebugcon, str[i]);
#endif
    }

    void WriteNs16550(const char* str, size_t length)
    {
#ifdef __x86_64__
        for (size_t i = 0; i < length; i++)
        {
            while ((In8(PortSerial + Ns16550Reg::LineStatus) & (1 << 5)) == 0);
            Out8(PortSerial + Ns16550Reg::Data, str[i]);
        }
#else
        for (size_t i = 0; i < length; i++)
        {
            while ((ns16550mmio.Offset(Ns16550Reg::LineStatus).VolatileRead<uint8_t>() & (1 << 5)) == 0);
            ns16550mmio.Offset(Ns16550Reg::Data).VolatileWrite<uint8_t>(str[i]);
        }
#endif
    }

    //NOTE: EnableLogBackend(); is defined in Log.cpp
}
