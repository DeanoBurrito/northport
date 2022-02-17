#include <drivers/DriverManager.h>
#include <devices/pci/BochsGraphicsAdaptor.h>

/*
    Located in a separate file as this may include from lots of different areas,
    in order to avoid polluting the DriverManager implementation, these are done here.
*/
namespace Kernel::Drivers
{
    constexpr const uint8_t machineName_bochsVideoAdaptor[] = { 0x11, 0x11, 0x34, 0x12 };
    
    void DriverManager::RegisterBuiltIns()
    {
        { //bochs standard vga: also used by qemu
            DriverManifest manifest;
            manifest.name = "Bochs Graphics Adaptor";
            manifest.machineName.length = 4;
            manifest.machineName.name = machineName_bochsVideoAdaptor;
            manifest.subsystem = DriverSubsytem::PCI;
            manifest.loadFrom = nullptr;
            manifest.status = sl::EnumSetFlag(DriverStatusFlags::None, DriverStatusFlags::BuiltIn);
            manifest.status = sl::EnumSetFlag(manifest.status, DriverStatusFlags::Loaded); //builtins are always loaded
            manifest.CreateNew = Devices::Pci::CreateNewBgaDriver;

            RegisterDriver(manifest);
        }
    }
}
