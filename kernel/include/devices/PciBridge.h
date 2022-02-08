#pragma once

#include <stdint.h>
#include <stddef.h>
#include <Platform.h>
#include <containers/Vector.h>
#include <drivers/DriverManifest.h>
#include <Optional.h>

#define VENDOR_ID_NONEXISTENT 0xFFFF
#define PCI_ENABLE_CONFIG_CYCLE 0x80000000

namespace Kernel::Devices
{
    struct PciFunction;
    struct PciDevice;
    struct PciBus;
    struct PciSegmentGroup;
    class PciBridge;

    struct PciAddress
    {
        uint64_t addr;

        FORCE_INLINE static PciAddress CreateLegacy(uint8_t bus, uint8_t device, uint8_t function, uint8_t regOffset)
        {
            //We set the upper 32 bits to indicate this is a legacy address.
            //This region in virtual memory is occupied by the kernel, so it'd cause other issues if an ecam address was in this range.
            return (bus << 16) | ((device & 0b11111) << 11) | ((function & 0b111) << 8) | (regOffset & 0b1111'1100) | (0xFFFF'FFFFul << 32);
        }
        
        FORCE_INLINE static PciAddress CreateEcam(NativeUInt segmentBase, uint8_t bus, uint8_t device, uint8_t function, uint8_t regOffset)
        {
            NativeUInt addend = (bus << 20) | (device << 15) | (function << 12) | (regOffset & 0xFFC);
            return segmentBase + addend;
        }

        FORCE_INLINE PciAddress(NativeUInt address) : addr(address)
        {}

        uint32_t ReadReg(size_t index);
        void WriteReg(size_t index, uint32_t data);
        FORCE_INLINE bool IsLegacy() const
        { return (addr >> 32) == 0xFFFF'FFFF; }
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

        FORCE_INLINE size_t GetId() const
        { return id; }
        FORCE_INLINE const PciConfigHeader* GetHeader() const
        { return &header; }
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

        FORCE_INLINE size_t GetId() const
        { return id; }
        FORCE_INLINE const PciBus* GetParent() const
        { return parent; }
        FORCE_INLINE uint8_t GetFunctionsBitmap() const
        { return functionBitmap; }
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

        FORCE_INLINE size_t GetId() const
        { return id; }
        FORCE_INLINE const PciSegmentGroup* GetParent() const
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
    
    struct DelayLoadedPciDriver
    {
        PciFunction* function;
        const Drivers::DriverManifest* manifest;

        DelayLoadedPciDriver(PciFunction* func, const Drivers::DriverManifest* mani) : function(func), manifest(mani)
        {}
    };

    class PciBridge
    {
    friend PciFunction;
    friend PciDevice;
    friend PciBus;
    friend PciSegmentGroup;

    private:
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

#undef ALLOW_PCI_INTERNAL_ACCESS
}
