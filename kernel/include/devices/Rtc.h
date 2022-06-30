#pragma once

#include <stdint.h>

namespace Kernel::Devices
{
    enum RtcRegisters : uint8_t 
    {
        Seconds = 0x00,
        Minutes = 0x02,
        Hours = 0x04,
        WeekDay = 0x06,
        DayOfMonth = 0x07,
        Month = 0x08,
        Year = 0x09,

        StatusPortA = 0x0A,
        StatusPortB = 0x0B
    };

    uint8_t ReadRtcRegister(RtcRegisters rtcRegister);
    uint64_t ConvertBCDToBinary(uint64_t value);
    uint64_t ReadRtcTime();
    bool IsRtcUpdating();
}
