#include <devices/Rtc.h>
#include <Cpu.h>
#include <Log.h>

namespace Kernel::Devices 
{
    uint8_t second;
    uint8_t minute;
    uint8_t hour;
    uint8_t day;
    uint8_t month;
    uint8_t year;

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
        Logf("Is 24 hours? %b - Is binary: %b", LogSeverity::Warning , is24Hours, isBinary);
        uint8_t seconds = readRtcRegister(0x00);
        uint8_t minutes = readRtcRegister(0x02);
        uint8_t hours = readRtcRegister(0x04);
        Logf("Seconds: %x", LogSeverity::Warning, seconds);
        Logf("Minutes: %x", LogSeverity::Warning, minutes);
        Logf("Hours: %x", LogSeverity::Warning, hours);
        return 0;
    }

    bool isRtcUpdating()
    {
        CPU::PortWrite8(CMOS_ADDRESS_REGISTER, 0x0A);
        return CPU::PortRead8(CMOS_DATA_REGISTER) & 0x80;
    }
}
