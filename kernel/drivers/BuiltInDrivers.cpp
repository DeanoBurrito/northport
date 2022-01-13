#include <drivers/DriverManager.h>
#include <devices/pci/QemuStandardVga.h>

/*
    Located in a separate file as this may include from lots of different areas,
    in order to avoid polluting the DriverManager implementation, these are done here.
*/
namespace Kernel::Drivers
{
    constexpr const uint8_t machineName_qemuStandardVga[] = { 0x11, 0x11, 0x34, 0x12 };
    
    void DriverManager::RegisterBuiltIns()
    {
        { //qemu standard vga
            DriverManifest manifest;
            manifest.name = "Qemu Standard VGA";
            manifest.machineName.length = 4;
            manifest.machineName.name = machineName_qemuStandardVga;
            manifest.subsystem = DriverSubsytem::PCI;
            manifest.loadFrom = nullptr;
            manifest.status = sl::EnumSetFlag(DriverStatusFlags::None, DriverStatusFlags::BuiltIn);
            manifest.status = sl::EnumSetFlag(manifest.status, DriverStatusFlags::Loaded); //builtins are always loaded
            
            using namespace Kernel::Devices::Pci::QemuStandardVga;
            manifest.InitNew = InitNew;
            manifest.Destroy = Destroy;
            manifest.HandleEvent = HandleEvent;

            RegisterDriver(manifest);
        }
    }
}
