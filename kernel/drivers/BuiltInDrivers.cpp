#include <drivers/DriverManager.h>
#include <devices/pci/BochsGraphicsAdaptor.h>
#include <devices/ps2/Ps2Driver.h>
#include <devices/pci/VirtioGraphics.h>
#include <filesystem/InitDiskFSDriver.h>

/*
    Located in a separate file as this may include from lots of different areas,
    in order to avoid polluting the DriverManager implementation, these are done here.
*/
namespace Kernel::Drivers
{
    constexpr const uint8_t machineName_bochsVideoAdaptor[] = { 0x11, 0x11, 0x34, 0x12 };
    constexpr const uint8_t machineName_initDiskFs[] = { "initdisk" };
    constexpr const uint8_t machineName_ps2[] = { "x86ps2" };
    constexpr const uint8_t machineName_virtioGpu[] = { 0x50, 0x10, 0xF4, 0x1A };
    
    void DriverManager::RegisterBuiltIns()
    {
        { //bochs standard vga: also used by qemu
            DriverManifest manifest;
            manifest.name = "Bochs Graphics Adaptor";
            manifest.machineName.length = 4;
            manifest.machineName.name = machineName_bochsVideoAdaptor;
            manifest.subsystem = DriverSubsystem::PCI;
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
            manifest.subsystem = DriverSubsystem::Filesystem;
            manifest.loadFrom = nullptr;
            manifest.status = sl::EnumSetFlag(DriverStatusFlags::Loaded, DriverStatusFlags::BuiltIn);
            manifest.CreateNew = Filesystem::CreateNewInitDiskFSDriver;

            RegisterDriver(manifest);
        }

        { //ps2 driver
            DriverManifest manifest;
            manifest.name = "PS/2 peripherals";
            manifest.machineName.length = 6;
            manifest.machineName.name = machineName_ps2;
            manifest.subsystem = DriverSubsystem::None;
            manifest.loadFrom = nullptr;
            manifest.status = sl::EnumSetFlag(DriverStatusFlags::Loaded, DriverStatusFlags::BuiltIn);
            manifest.CreateNew = Devices::Ps2::CreateNewPs2Driver;

            RegisterDriver(manifest);
        }

        { //virtio gpu driver
            DriverManifest manifest;
            manifest.name = "VirtIO GPU";
            manifest.machineName.length = 4;
            manifest.machineName.name = machineName_virtioGpu;
            manifest.subsystem = DriverSubsystem::PCI;
            manifest.loadFrom = nullptr;
            manifest.status = sl::EnumSetFlag(DriverStatusFlags::Loaded, DriverStatusFlags::BuiltIn);
            manifest.CreateNew = Devices::Pci::CreateNewVirtioGpuDriver;

            RegisterDriver(manifest);
        }
    }
}
