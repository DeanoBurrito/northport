#pragma once

#include <devices/PciAddress.h>

namespace Npk::Drivers
{
    enum class InitTagType : size_t
    {
        Pci,
        Mmio,
    };

    struct InitTag
    {
        const InitTagType type;
        InitTag* const next;

        InitTag(InitTagType type, InitTag* next) : type(type), next(next)
        {}
    };

    struct PciInitTag : public InitTag
    {
        Devices::PciAddress address;

        PciInitTag(Devices::PciAddress addr, InitTag* next)
        : InitTag(InitTagType::Pci, next), address(addr)
        {}
    };

    struct MmioInitTag : public InitTag
    {
        uintptr_t address;

        MmioInitTag(uintptr_t addr, InitTag* next)
        : InitTag(InitTagType::Mmio, next), address(addr)
        {}
    };
}
