#pragma once

#include <config/DeviceTree.h>
#include <devices/PciAddress.h>
#include <drivers/DriverManifest.h>
#include <Optional.h>

namespace Npk::Drivers
{
    enum class InitTagType : size_t
    {
        Pci,
        DeviceTree,
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

    struct DeviceTreeInitTag : public InitTag
    {
        Config::DtNode node;

        DeviceTreeInitTag(Config::DtNode node, InitTag* next)
        : InitTag(InitTagType::DeviceTree, next), node(node)
        {}
    };

    sl::Opt<InitTag*> FindTag(void* tags, InitTagType type);
    void CleanupTags(void* tags);
}
