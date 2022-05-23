#pragma once

#include <stdint.h>
#include <stdbool.h>

#define CMOS_ADDRESS_REGISTER   0x70
#define CMOS_DATA_REGISTER  0x71
namespace Kernel::Devices
{

    enum Rtc_Registers : uint8_t 
    {
        Seconds = 0x00,
        Minutes = 0x02,
        Hours = 0x04,
        WeekDay = 0x06,
        DayOfMonth = 0x07,
        Month = 0x08,
        Year = 0x09

    };

    uint64_t readRtcTime();
    bool isRtcUpdating();
}
