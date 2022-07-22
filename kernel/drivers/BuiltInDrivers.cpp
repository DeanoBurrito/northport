#include <drivers/DriverManager.h>
#include <devices/pci/BochsGraphicsAdaptor.h>
#include <devices/ps2/Ps2Driver.h>
#include <devices/pci/VirtioGraphics.h>
#include <filesystem/InitDiskFSDriver.h>
#include <devices/pci/NvmeController.h>

/*
    Located in a separate file as this may include from lots of different areas,
    in order to avoid polluting the DriverManager implementation, these are done here.
    TODO: some of these really belong in separate binaries.
*/
namespace Kernel::Drivers
{
    constexpr const uint8_t machineName_bochsVideoAdaptor[] = { PciIdMatching, 0x34, 0x12, 0x11, 0x11 };
    constexpr const uint8_t machineName_initDiskFs[] = { "initdisk" };
    constexpr const uint8_t machineName_ps2[] = { "x86ps2" };
    constexpr const uint8_t machineName_virtioGpu[] = { PciIdMatching, 0xF4, 0x1A, 0x50, 0x10 };
    constexpr const uint8_t machineName_nvmeController[] = { PciClassMatching, 2, 8, 1}; //class 1, subclass 8, interface 2 (io controller)
    
    void DriverManager::RegisterBuiltIns()
    {
        { //bochs standard vga: also used by qemu
            DriverManifest manifest;
            manifest.name = "Bochs Graphics Adaptor";
            manifest.machineName.length = 5;
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
            manifest.machineName.length = 5;
            manifest.machineName.name = machineName_virtioGpu;
            manifest.subsystem = DriverSubsystem::PCI;
            manifest.loadFrom = nullptr;
            manifest.status = sl::EnumSetFlag(DriverStatusFlags::Loaded, DriverStatusFlags::BuiltIn);
            manifest.CreateNew = Devices::Pci::CreateNewVirtioGpuDriver;

            RegisterDriver(manifest);
        }


    }
}
