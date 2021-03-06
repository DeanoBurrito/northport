#pragma once

#include <NativePtr.h>
#include <stdint.h>
#include <drivers/GenericDriver.h>

namespace Kernel::Drivers
{
    enum class DriverSubsystem
    {
        None = 0,

        PCI = 1,
        Filesystem = 2,
    };

    //prefixes for pci driver machine names.
    constexpr uint8_t PciIdMatching = 'I';
    constexpr uint8_t PciClassMatching = 'C';

    enum class DriverStatusFlags : uint64_t
    {
        None = 0,

        //set if code/data has been loaded, clear otherwise
        Loaded = 1 << 0,
        //set if driver is actively running
        Running = 1 << 1,
        //set if driver is built in to kernel, and always available. Otherwise go fish.
        BuiltIn = 1 << 2,
    };
    
    struct DriverMachineName
    {
        size_t length;
        const uint8_t* name;
    };

    using DriverCreateNewCallback = GenericDriver* (*)();

    struct DriverManifest
    {
        DriverSubsystem subsystem;
        DriverStatusFlags status;
        const char* loadFrom; //filepath of driver file, nullptr if builtin
        const char* name; //user-friendly display name
        DriverMachineName machineName; //user-unfriendly name

        //returns a pointer to a derived GenericDriver* of the correct type. Thats it.
        DriverCreateNewCallback CreateNew;
    };
}
