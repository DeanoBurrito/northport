#include <devices/Rtc.h>
#include <Cpu.h>

namespace KernelMain::Devices 
{

    uint64_t readRtcTime() 
    {
        return 0;
    }

    bool isRtcUpdating()
    {
        CPU::PortWrite8(CMOS_ADDRESS_REGISTER, 0x0A);       
        return false;
    }
}
