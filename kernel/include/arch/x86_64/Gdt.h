#pragma once

#include <stdint.h>
#include <stddef.h>

namespace Kernel
{
    struct [[gnu::packed]] Gdtr
    {
        uint16_t limit;
        uint64_t address;
    };

    //NOTE: does not lock the tss desciptor. See Tss.cpp:FlushTSS() for that.
    void SetTssDescriptorAddr(uint64_t newAddr);

    [[gnu::naked]]
    void FlushGDT();
}