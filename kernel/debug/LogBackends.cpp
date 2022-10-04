#include <debug/LogBackends.h>
#include <debug/Terminal.h>
#include <debug/TerminalImage.h>
#include <arch/Platform.h>
#include <stdint.h>

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
    
    void InitTerminal()
    {
#ifdef NP_INCLUDE_TERMINAL_BG
        GTStyle style { DEFAULT_ANSI_COLOURS, DEFAULT_ANSI_BRIGHT_COLOURS, 0x68000000, 0xDDDDDD, 12 };
        OpenImage(termBg, (uintptr_t)BackgroundBegin, (uintptr_t)BackgroundEnd - (uintptr_t)BackgroundBegin);
        GTBackground bg { &termBg, 0 };
#else
        GTStyle style { DEFAULT_ANSI_COLOURS, DEFAULT_ANSI_BRIGHT_COLOURS, 0x0F0404, 0xDDDDDD, 12 };
        GTBackground bg { NULL,  0 };
#endif
        term.Init(style, bg);
    }

    void WriteTerminal(const char* str, size_t length)
    {
        term.Write(str, length);
    }

    bool serialInitialized = false;
    void InitSerial()
    {
        if (serialInitialized)
            return;
        
        serialInitialized = true;
#ifdef NP_X86_64_E9_ALLOWED
        return;
#endif
    }

    void WriteSerial(const char* str, size_t length)
    {
        if (!serialInitialized)
            return;
#ifdef NP_X86_64_E9_ALLOWED
        for (size_t i = 0; i < length; i++)
            Out8(PortDebugcon, str[i]);
#endif
    }
}
