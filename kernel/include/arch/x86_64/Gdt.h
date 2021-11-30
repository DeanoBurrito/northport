#pragma once

#include <stdint.h>

namespace Kernel
{
    extern uint64_t defaultGdt[];

    struct [[gnu::packed]] GDTR
    {
        uint16_t limit;
        uint64_t address;
    };

    void SetupGDT();
    
    [[gnu::naked]]
    void FlushGDT();
}