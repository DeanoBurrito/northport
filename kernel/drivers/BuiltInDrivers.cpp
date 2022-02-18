#include <drivers/DriverManager.h>
#include <devices/pci/BochsGraphicsAdaptor.h>
#include <filesystem/InitDiskFSDriver.h>

/*
    Located in a separate file as this may include from lots of different areas,
    in order to avoid polluting the DriverManager implementation, these are done here.
*/
namespace Kernel::Drivers
{
    constexpr const uint8_t machineName_bochsVideoAdaptor[] = { 0x11, 0x11, 0x34, 0x12 };
    constexpr const uint8_t machineName_initDiskFs[] = { 'i', 'n', 'i', 't', 'd', 'i', 's', 'k' };
    
    void DriverManager::RegisterBuiltIns()
    {
        { //bochs standard vga: also used by qemu
            DriverManifest manifest;
            manifest.name = "Bochs Graphics Adaptor";
            manifest.machineName.length = 4;
            manifest.machineName.name = machineName_bochsVideoAdaptor;
            manifest.subsystem = DriverSubsytem::PCI;
            manifest.loadFrom = nullptr;
            manifest.status = sl::EnumSetFlag(DriverStatusFlags::Loaded, DriverStatusFlags::BuiltIn); //builtins cant ever be 'unloaded', as that would mean unloading the kernel.
            manifest.CreateNew = Devices::Pci::CreateNewBgaDriver;

            RegisterDriver(manifest);
        }

        { //initdisk filesystem driver
            DriverManifest manifest;
            manifest.name = "InitDisk FS";
            manifest.machineName.length = 8;
            manifest.machineName.name = machineName_initDiskFs;
            manifest.subsystem = DriverSubsytem::Filesystem;
            manifest.loadFrom = nullptr;
            manifest.status = sl::EnumSetFlag(DriverStatusFlags::Loaded, DriverStatusFlags::BuiltIn);
            manifest.CreateNew = Filesystem::CreateNewInitDiskFSDriver;

            RegisterDriver(manifest);
        }
    }
}
