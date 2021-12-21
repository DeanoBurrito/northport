#pragma once

#include <stddef.h>
#include <stdint.h>

namespace Kernel::Devices
{
    //default setup of 1000.151Hz (divisor of 1193)
    void InitPit(uint8_t destApicId, uint8_t vectorNum);
    //masks interrupts from the PIT, also clears tick count
    void SetPitMasked(bool masked);

    //1 tick roughly equals 1ms, measured since last time pit was unmasked
    size_t GetPitTicks();
    void PitHandleIrq();
}
