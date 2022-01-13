#pragma once

#include <stdint.h>
#include <stddef.h>

namespace Kernel::Devices
{
    struct PciDevice;
    struct PciFunction;
}

namespace Kernel::Drivers
{
    enum class DriverInitTagType : uint64_t
    {
        CustomData = 0,
        PciFunction = 1,
    };

    struct DriverInitTag
    {
        const DriverInitTagType type;
        DriverInitTag* next;

        DriverInitTag() = delete;
        DriverInitTag(DriverInitTagType type) : type(type)
        {}
    };

    struct DriverInitTagCustom : public DriverInitTag
    {
        const void* data;
        DriverInitTagCustom() = delete;
        DriverInitTagCustom(void* data) : DriverInitTag(DriverInitTagType::CustomData), data(data)
        {}
    };

    struct DriverInitTagPciFunction : public DriverInitTag
    {
        Devices::PciFunction* function;

        DriverInitTagPciFunction() = delete;
        DriverInitTagPciFunction(Devices::PciFunction* func)
        : DriverInitTag(DriverInitTagType::PciFunction), function(func)
        {}
    };
}
