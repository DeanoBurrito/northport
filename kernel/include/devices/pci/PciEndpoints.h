#pragma once

#include <stdint.h>
#include <stddef.h>
#include <Platform.h>
#include <devices/pci/PciAddress.h>
#include <containers/Vector.h>
#include <Optional.h>

namespace Kernel::Devices
{ class PciBridge; }

namespace Kernel::Devices::Pci
{
    struct PciDevice;
    struct PciBus;
    struct PciSegmentGroup;

    struct [[gnu::packed]] PciCap
    {
        //identifies how to process the rest of this structure
        uint8_t capabilityId;
        //offset in bytes from the base of the pci descriptor, NOT from this struct.
        uint8_t nextOffset; 
    };

    struct PciBar
    {
        uint64_t address;
        size_t size;
        bool isMemory;
        bool isPrefetchable;
        bool is64BitWide;
    };
    
    struct PciConfigHeader
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

        PciBar bars[6];
    };

    sl::Opt<PciCap*> FindPciCap(PciAddress addr, uint8_t withId, PciCap* start = nullptr);

    struct PciFunction
    {
    friend PciDevice;
    friend PciBus;
    friend PciSegmentGroup;
    friend PciBridge;
    
    private:
        PciConfigHeader header;
        PciAddress addr;
        PciDevice* parent;
        uint8_t id;

        void Init();

    public:
        char lock;

        PciFunction(uint8_t id, PciDevice* parent) : addr(0), parent(parent), id(id)
        {}

        FORCE_INLINE size_t Id() const
        { return id; }
        FORCE_INLINE const PciConfigHeader* Header() const
        { return &header; }
        FORCE_INLINE PciAddress Address() const
        { return addr; }
    };

    struct PciDevice
    {
    friend PciFunction;
    friend PciBus;
    friend PciSegmentGroup;
    friend PciBridge;

    private:
        sl::Vector<PciFunction> functions;
        uint8_t functionBitmap;
        PciBus* parent;
        PciAddress addr;
        uint8_t id;

        void Init();

    public:
        PciDevice(uint8_t id, PciBus* parent) : parent(parent), addr(0), id(id)
        {}

        FORCE_INLINE size_t Id() const
        { return id; }
        FORCE_INLINE const PciBus* Parent() const
        { return parent; }
        FORCE_INLINE uint8_t FunctionsBitmap() const
        { return functionBitmap; }
        FORCE_INLINE PciAddress Address() const
        { return addr; }
        sl::Opt<const PciFunction*> GetFunction(size_t index) const;
    };

    struct PciBus
    {
    friend PciFunction;
    friend PciDevice;
    friend PciSegmentGroup;
    friend PciBridge;

    private:
        sl::Vector<PciDevice> devices;
        sl::Vector<PciBus> children;

        size_t id;
        PciSegmentGroup* parent;

    public:
        PciBus(size_t id, PciSegmentGroup* parent) : id(id), parent(parent)
        {}

        FORCE_INLINE size_t Id() const
        { return id; }
        FORCE_INLINE const PciSegmentGroup* Parent() const
        { return parent; }
    };

    struct PciSegmentGroup
    {
    friend PciFunction;
    friend PciDevice;
    friend PciBus;
    friend PciBridge;

    private:
        size_t id;
        NativeUInt baseAddress;
        sl::Vector<PciBus> children;

        void Init();

    public:
        PciSegmentGroup(size_t id, NativeUInt base) : id(id), baseAddress(base)
        {}

        FORCE_INLINE NativeUInt GetBaseAddress() const
        { return baseAddress; }
        FORCE_INLINE size_t GetId() const
        { return id; }
    };
}
