#include <devices/Rtc.h>
#include <Cpu.h>
#include <Log.h>

namespace Kernel::Devices 
{
    uint64_t daysPerMonth[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

    uint8_t ReadRtcRegister(uint8_t rtcRegister)
    {
        CPU::PortWrite8(CMOS_ADDRESS_REGISTER, rtcRegister);
        return CPU::PortRead8(CMOS_DATA_REGISTER);
    }
    
    uint64_t ReadRtcTime() 
    {
        while ( IsRtcUpdating() );

        uint8_t rtcRegisterStatusB = ReadRtcRegister(0x0B);
        bool is24Hours = rtcRegisterStatusB & 0x02;
        bool isBinary = rtcRegisterStatusB & 0x04;

        uint8_t seconds = ReadRtcRegister(Seconds);
        uint8_t minutes = ReadRtcRegister(Minutes);
        uint8_t hours = ReadRtcRegister(Hours);
        uint8_t year = ReadRtcRegister(Year);
        uint8_t dayofmonth = ReadRtcRegister(DayOfMonth);
        uint8_t month = ReadRtcRegister(Month);
        uint8_t last_seconds; 
        uint8_t last_minutes;
        uint8_t last_hours;
        uint8_t last_year;
        uint8_t last_dayofmonth;
        uint8_t last_month;

        do 
        {
            last_seconds = seconds;
            last_minutes = minutes;
            last_hours = hours;
            last_year = year;
            last_dayofmonth = dayofmonth;
            last_month = month;
    
            while ( IsRtcUpdating() );
            seconds = ReadRtcRegister(Seconds);
            minutes = ReadRtcRegister(Minutes);
            hours = ReadRtcRegister(Hours);
            year = ReadRtcRegister(Year);
            dayofmonth = ReadRtcRegister(DayOfMonth);
            month = ReadRtcRegister(Month);
        }
        while ( last_seconds == seconds && last_minutes == minutes && last_hours == hours && last_year == year &&  last_dayofmonth == dayofmonth && last_month == month);

        if ( !isBinary) 
        {
            hours = ConvertBCDToBinary(hours);
            seconds = ConvertBCDToBinary(seconds);
            minutes = ConvertBCDToBinary(minutes);
            year = ConvertBCDToBinary(year);
            month = ConvertBCDToBinary(month);
            dayofmonth = ConvertBCDToBinary(dayofmonth);
        }

        uint64_t yearsSinceEpoch = (BASE_CENTURY + year) - 1970; // Let's count the number of years passed since the Epoch Year: (1970)
        uint64_t leapYears = yearsSinceEpoch / 4; // We need to know how many leap years too...
        bool addAnotherLeapYear = (yearsSinceEpoch % 4) > 1; // if yearsSinceEpoch % 4 is greater/equal than 2 we have to add another leap year
        
        if (addAnotherLeapYear)
        {
            leapYears++;
        }
        uint64_t daysCurrentYear = 0;
        for (int i = 0; i < month-1; i++) 
        {
            daysCurrentYear += daysPerMonth[i];
        }
        
        daysCurrentYear = daysCurrentYear + (dayofmonth);
        if ( !is24Hours && (hours & 0x80) )
        {
            hours = ((hours & 0x7F) + 12) % 24;
        }

        uint64_t daysSinceEpoch = (yearsSinceEpoch * 365) - 1; 
        uint64_t unixTimeOfDay = (hours * 3600) + (minutes * 60) + seconds;
        uint64_t secondsSinceEpoch = (daysSinceEpoch * 86400) + (leapYears * 86400) + (daysCurrentYear * 86400) + unixTimeOfDay;
        return secondsSinceEpoch;
    }


    uint64_t ConvertBCDToBinary(uint64_t value) 
    {
        return ((value / 16) * 10) + (value & 0x0f);
    }

    bool IsRtcUpdating()
    {
        CPU::PortWrite8(CMOS_ADDRESS_REGISTER, 0x0A);
        return CPU::PortRead8(CMOS_DATA_REGISTER) & 0x80;
    }
}
