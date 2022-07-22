#pragma once

#include <stdint.h>
#include <devices/pci/PciAddress.h>
#include <Optional.h>

namespace Kernel::Devices::Pci
{
    constexpr uint8_t CapIdReserved = 0x0;
    constexpr uint8_t CapIdPowerManagement = 0x1;
    constexpr uint8_t CapIdAgp = 0x2;
    constexpr uint8_t CapidVpd = 0x3;
    constexpr uint8_t CapIdSlotId = 0x4;
    constexpr uint8_t CapIdMsi = 0x5;
    constexpr uint8_t CapIdCompactHotSwap = 0x6;
    constexpr uint8_t CapIdPciX = 0x7;
    constexpr uint8_t CapIdHyperTransport = 0x8;
    constexpr uint8_t CapIdVendor = 0x9;
    constexpr uint8_t CapIdDebugPort = 0xA;
    constexpr uint8_t CapIdCompactResCtrl = 0xB;
    constexpr uint8_t CapIdHotPlug = 0xC;
    constexpr uint8_t CapIdBridgeVendorId = 0xD;
    constexpr uint8_t CapIdAgp8x = 0xE;
    constexpr uint8_t CapIdSecureDevice = 0xF;
    constexpr uint8_t CapIdPciExpress = 0x10;
    constexpr uint8_t CapIdMsiX = 0x11;

    struct [[gnu::packed]] PciCap
    {
        uint8_t capabilityId;
        //offset in bytes from the base of the pci config space, NOT from this struct.
        uint8_t nextOffset; 
    };

    struct [[gnu::packed]] PciCapMsi : public PciCap
    {
        uint16_t messageControl;
        
        bool Enabled() const;
        void Enable(bool yes);

        size_t VectorsRequested() const;
        void SetVectorsEnabled(size_t count);

        bool Has64BitAddress() const;
        void SetAddress(sl::NativePtr ptr);
        void SetData(uint16_t data);
        
        bool Masked(size_t index) const;
        void Mask(size_t index, bool masked);
        bool Pending(size_t index) const;
    };

    struct [[gnu::packed]] PciCapMsiX : public PciCap
    {
    private:
        sl::NativePtr GetTableEntry(size_t index, PciAddress addr) const;

    public:
        uint16_t messageControl;
        uint32_t table; //contains both the offset, and the BIR in lower 2 bits
        uint32_t pendingBitArray;

        bool Enabled() const;
        void Enable(bool yes);

        size_t Vectors() const;
        void SetVector(size_t index, uint64_t address, uint16_t data, PciAddress addr);

        bool Masked(size_t index, PciAddress addr) const;
        void Mask(size_t index, bool masked, PciAddress addr);
        bool Pending(size_t index, PciAddress addr) const;
    };

    sl::Opt<PciCap*> FindPciCap(PciAddress addr, uint8_t withId, PciCap* start = nullptr);
}
