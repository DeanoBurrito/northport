#pragma once

#include <stdint.h>
#include <stddef.h>
#include <containers/Vector.h>
#include <Optional.h>

#define VENDOR_ID_NONEXISTENT 0xFFFF
#define PCI_ENABLE_CONFIG_CYCLE 0x80000000

namespace Kernel::Devices
{
    struct PciDevice;
    struct PciBus;
    struct PciSegmentGroup;
    class PciBridge;
#define ALLOW_PCI_INTERNAL_ACCESS friend PciDevice; friend PciBus; friend PciSegmentGroup; friend PciBridge;
    
    struct PciBar
    {
        uint64_t address;
        size_t size;
        bool isMemory;
    };
    
    struct PciDeviceHeader
    {
        struct
        {
            uint16_t vendor;
            uint16_t device;
            uint16_t subsystem;
            uint16_t subsystemVendor;
            uint8_t deviceClass;
            uint8_t deviceSubclass;
        } ids;

        uint8_t revision;
        uint8_t progIf;
        uint8_t capabilitiesOffset; //offset in config space of capabilities list
        uint8_t interruptVector;
        uint8_t functionsBitmap;

        PciBar bars[6];

        NativeUInt ecamAddress;
        uint32_t legacyAddress;
    };

    struct PciDevice
    {
    ALLOW_PCI_INTERNAL_ACCESS
    private:
        size_t id;
        PciDeviceHeader header;

        void Init(size_t devNum);

    public:
        size_t GetId() const;
        const PciDeviceHeader* GetHeader() const;
    };

    struct PciBus
    {
    ALLOW_PCI_INTERNAL_ACCESS
    private:
        size_t id;

        sl::Vector<PciDevice> devices;
        sl::Vector<PciBus> children;
        size_t firstChildId;
        size_t lastChildId;

    public:
        PciBus() : id(VENDOR_ID_NONEXISTENT), firstChildId(VENDOR_ID_NONEXISTENT), lastChildId(VENDOR_ID_NONEXISTENT)
        {}

        PciBus(size_t id, size_t firstChild, size_t lastChild) : id(id), firstChildId(firstChild), lastChildId(lastChild)
        {}
    };

    struct PciSegmentGroup
    {
    ALLOW_PCI_INTERNAL_ACCESS
    private:
        size_t id;
        NativeUInt baseAddress;
        sl::Vector<PciBus> children;
        size_t firstBus;
        size_t lastBus;

        void Init();

    public:
        PciSegmentGroup(size_t id, NativeUInt base) : id(id), baseAddress(base), firstBus(0), lastBus(0)
        {}

        PciSegmentGroup(size_t id, NativeUInt base, size_t firstBus, size_t lastBus) : id(id), baseAddress(base), firstBus(firstBus), lastBus(lastBus)
        {}

        NativeUInt GetBaseAddress() const;
        size_t GetId() const;
    };

    class PciBridge
    {
    ALLOW_PCI_INTERNAL_ACCESS
    private:
        bool ecamAvailable;
        sl::Vector<PciSegmentGroup>* segments;

        static void LegacyWriteConfig(uint32_t address, uint32_t data);
        static uint32_t LegacyReadConfig(uint32_t address);

        static void EcamWriteConfig(NativeUInt address, uint32_t data);
        static uint32_t EcamReadConfig(NativeUInt address);

    public:
        static PciBridge* Global();
        static uint32_t CreateLegacyConfigAddr(size_t busNumber, size_t deviceNumber, size_t functionNumber, size_t reg);
        static NativeUInt CreateEcamConfigAddr(size_t segment, size_t bus, size_t device, size_t function, size_t reg);

        void Init();

        bool EcamAvailable() const;
        sl::Opt<const PciSegmentGroup*> GetSegment(size_t id) const;
        sl::Opt<const PciBus*> GetBus(size_t segment, size_t bus) const;
        sl::Opt<const PciDevice*> GetDevice(size_t segment, size_t bus, size_t device) const;
    };

#undef ALLOW_PCI_INTERNAL_ACCESS
}
