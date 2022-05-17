#pragma once

#include <stdint.h>
#include <stddef.h>
#include <devices/pci/PciAddress.h>
#include <Optional.h>

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
        DriverInitTag(DriverInitTagType type) : type(type), next(nullptr)
        {}
    };

    struct DriverInitTagCustom : public DriverInitTag
    {
        const void* data;
        DriverInitTagCustom() = delete;
        DriverInitTagCustom(void* data) : DriverInitTag(DriverInitTagType::CustomData), data(data)
        {}
    };

    struct DriverInitTagPci : public DriverInitTag
    {
        Devices::Pci::PciAddress address;

        DriverInitTagPci() = delete;
        DriverInitTagPci(Devices::Pci::PciAddress addr)
        : DriverInitTag(DriverInitTagType::PciFunction), address(addr)
        {}
    };

    struct DriverInitInfo
    {
        size_t id;
        DriverInitTag* next;

        sl::Opt<DriverInitTag*> FindTag(DriverInitTagType type, DriverInitTag* start = nullptr);
    };
}
