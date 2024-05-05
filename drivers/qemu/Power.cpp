#include <Power.h>
#include <interfaces/driver/Drivers.h>

namespace Qemu
{
    bool QemuPowerOff(npk_device_api*)
    {
#ifdef __x86_64__
        asm volatile("outw %0, %1" :: "a"((uint16_t)0x2000), "Nd"((uint16_t)0x604));
#endif
        return false;
    }

    npk_sys_power_device_api powerApi
    {
        .header = 
        {
            .id = 0,
            .type = npk_device_api_type::SysPower,
            .driver_data = nullptr,
            .get_summary = nullptr
        },
        .power_off = QemuPowerOff,
        .reboot = nullptr,
    };

    void InitPowerDevice()
    {
#ifdef __x86_64__
        npk_add_device_api(&powerApi.header);
#endif
    }
};
