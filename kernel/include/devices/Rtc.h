#pragma once

#include <stdint.h>

#define CMOS_ADDRESS_REGISTER   0x70
#define CMOS_DATA_REGISTER  0x71
#define BASE_CENTURY 2000
#define SECONDS_IN_A_DAY 86400
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


    uint8_t ReadRtcRegister(uint8_t rtcRegister);
    uint64_t ConvertBCDToBinary(uint64_t value);
    uint64_t ReadRtcTime();
    bool IsRtcUpdating();
}
