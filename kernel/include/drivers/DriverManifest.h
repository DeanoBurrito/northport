#pragma once

#include <NativePtr.h>
#include <stdint.h>
#include <drivers/DriverInitTags.h>

namespace Kernel::Drivers
{
    enum class DriverSubsytem
    {
        None = 0,

        PCI = 1,
    };

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

    enum class DriverEventType : uint64_t
    {
        Unknown = 0,
    };

    struct DriverInitInfo
    {
        size_t id;
        DriverInitTag* next;
    };
    
    //returns an instance of a new driver. 
    using DriverCbInitNew = void* (*)(DriverInitInfo* initInfo);
    //dispose of an instance of this driver.
    using DriverCbDestroy = void (*)(void* inst);
    //kernel is passing on an event for the driver to handle. Inst is an instance returned by InitNew
    using DriverCbHandleEvent = void (*)(void* inst, DriverEventType type, void* arg);

    struct DriverMachineName
    {
        size_t length;
        const uint8_t* name;
    };

    struct DriverManifest
    {
        DriverSubsytem subsystem;
        DriverStatusFlags status;
        const char* loadFrom; //filepath of driver file, nullptr if builtin
        const char* name; //user-friendly display name
        DriverMachineName machineName; //user-unfriendly name

        //driver callbacks
        DriverCbInitNew InitNew;
        DriverCbDestroy Destroy;
        DriverCbHandleEvent HandleEvent;
    };
}
