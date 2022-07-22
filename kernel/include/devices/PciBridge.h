#pragma once

#include <devices/pci/PciAddress.h>
#include <drivers/DriverManifest.h>
#include <containers/Vector.h>

namespace Kernel::Devices
{
    using Pci::PciAddress;
    
    struct PciFunction;
    struct PciDevice;
    struct PciBus;
    struct PciSegmentGroup;

    struct PciFunction
    {
        uint8_t id;
        uint8_t deviceId;
        uint8_t busId;
        PciAddress address;
    };

    struct PciSegmentGroup
    {
        size_t id;
        sl::NativePtr baseAddress;
        sl::Vector<PciFunction> functions;

    private:
        void TryFindDrivers(PciAddress addr);
    public:

        void Init();
    };

    class PciBridge
    {
    friend PciSegmentGroup;
    private:
        struct DelayLoadedPciDriver
        {
            PciAddress address;
            const Drivers::DriverManifest* manifest;

            DelayLoadedPciDriver(PciAddress addr, const Drivers::DriverManifest* mani) : address(addr), manifest(mani)
            {}
        };

        bool ecamAvailable;
        sl::Vector<PciSegmentGroup>* segments;
        sl::Vector<DelayLoadedPciDriver>* pendingDriverLoads;

    public:
        static PciBridge* Global();

        void Init();
        FORCE_INLINE bool EcamAvailable() const
        { return ecamAvailable; }

        sl::Vector<PciAddress> FindFunctions(uint16_t vendorId, uint16_t deviceId, bool onlyOne = false);
    };
}
