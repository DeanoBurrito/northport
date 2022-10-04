#pragma once

#include <stdint.h>

namespace Npk
{
    constexpr uint16_t SelectorKernelCode = 0x08 | 0;
    constexpr uint16_t SelectorKernelData = 0x10 | 0;
    constexpr uint16_t SelectorUserData   = 0x18 | 3;
    constexpr uint16_t SelectorUserCode   = 0x20 | 3;
    constexpr uint16_t SelectorTss        = 0x28;
    
    //loads our current gdt, and reloads all selectors except GS.
    void FlushGdt();
}
