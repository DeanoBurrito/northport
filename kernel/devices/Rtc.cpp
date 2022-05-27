#include <devices/Rtc.h>
#include <Cpu.h>
#include <Log.h>

namespace Kernel::Devices 
{

    uint8_t readRtcRegister(uint8_t rtcRegister)
    {
        CPU::PortWrite8(CMOS_ADDRESS_REGISTER, rtcRegister);
        return CPU::PortRead8(CMOS_DATA_REGISTER);
    }
    
    uint64_t readRtcTime() 
    {
        while ( isRtcUpdating() );

        uint8_t rtcRegisterStatusB = readRtcRegister(0x0B);
        bool is24Hours = rtcRegisterStatusB & 0x02;
        bool isBinary = rtcRegisterStatusB & 0x04;
        Logf("Is 24 hours? %b - Is binary: %b - rtcRegisterStatusB: %d", LogSeverity::Warning , is24Hours, isBinary, rtcRegisterStatusB);
        uint8_t seconds = readRtcRegister(Seconds);
        uint8_t minutes = readRtcRegister(Minutes);
        uint8_t hours = readRtcRegister(Hours);
        uint8_t year = readRtcRegister(Year);
        uint8_t dayofmonth = readRtcRegister(DayOfMonth);
        uint8_t month = readRtcRegister(Month);
       if ( !isBinary) 
        {
            hours = convertBCDToBinary(hours);
            seconds = convertBCDToBinary(seconds);
            minutes = convertBCDToBinary(minutes);
            year = convertBCDToBinary(year);
        }
        Logf("Seconds: 0x%x", LogSeverity::Warning, seconds);        
        Logf("Minutes: 0x%x", LogSeverity::Warning, minutes);
        Logf("Hours: 0x%x", LogSeverity::Warning, hours);
        Logf("DayOfMonth: 0x%x", LogSeverity::Warning, dayofmonth);
        Logf("Month: 0x%d", LogSeverity::Warning, month);
        Logf("Year: 0x%d", LogSeverity::Warning, year);
 
        uint64_t yearsSinceEpoch = (BASE_CENTURY + year) - 1970;
        uint64_t daysSinceEpoch = (yearsSinceEpoch * 365) - 1; 
        uint64_t unixTimeOfDay = (hours * 3600) + (minutes * 60) + seconds;
        uint64_t secondsSinceEpoch = (daysSinceEpoch * 86400) + unixTimeOfDay;
        if ( secondsSinceEpoch > 1651104000) Log("Nearly", LogSeverity::Warning); 
        if ( secondsSinceEpoch > 2018345691) Log("Problem", LogSeverity::Warning);
        Logf("Current unix timestamp is: %lu", LogSeverity::Warning, secondsSinceEpoch);
        return secondsSinceEpoch;
    }

    uint64_t convertBCDToBinary(uint64_t value) 
    {
        return ((value / 16) * 10) + (value & 0x0f);
    }

    bool isRtcUpdating()
    {
        CPU::PortWrite8(CMOS_ADDRESS_REGISTER, 0x0A);
        return CPU::PortRead8(CMOS_DATA_REGISTER) & 0x80;
    }
}
