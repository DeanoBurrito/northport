#pragma once

#include <devices/pci/PciAddress.h>
#include <devices/pci/PciEndpoints.h>
#include <drivers/DriverManifest.h>

#define VENDOR_ID_NONEXISTENT 0xFFFF

namespace Kernel::Devices
{
    using Pci::PciFunction;
    using Pci::PciDevice;
    using Pci::PciBus;
    using Pci::PciSegmentGroup;

    /*
        This class was designed for x86, where both ECAM and a port io ISA bridge exist. However the port io
        functionality is only enabled if compiling for x86, leaving this as a generic ECAM pci bridge.
    */
    class PciBridge
    {
    friend PciFunction;
    friend PciDevice;
    friend PciBus;
    friend PciSegmentGroup;

    private:
        struct DelayLoadedPciDriver
        {
            Pci::PciAddress address;
            const Drivers::DriverManifest* manifest;

            DelayLoadedPciDriver(Pci::PciAddress addr, const Drivers::DriverManifest* mani) : address(addr), manifest(mani)
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

        sl::Opt<const PciSegmentGroup*> GetSegment(size_t id) const;
        sl::Opt<const PciBus*> GetBus(size_t segment, size_t bus) const;
        sl::Opt<const PciDevice*> GetDevice(size_t segment, size_t bus, size_t device) const;
        sl::Opt<const PciFunction*> GetFunction(size_t segment, size_t bus, size_t device, size_t function) const;

        sl::Opt<const PciDevice*> FindDevice(uint16_t vendorId, uint16_t deviceId) const;
    };
}
