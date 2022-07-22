#pragma once

#include <stdint.h>
#include <Platform.h>

namespace Kernel::Devices::Pci
{
    constexpr size_t PciRegId = 0;
    constexpr size_t PciRegCmdStatus = 1;
    constexpr size_t PciRegClass = 2;
    constexpr size_t PciRegLatencyCache = 3;
    constexpr size_t PciRegBar0 = 4;
    constexpr size_t PciRegBar1 = 5;
    constexpr size_t PciRegBar2 = 6;
    constexpr size_t PciRegBar3 = 7;
    constexpr size_t PciRegBar4 = 8;
    constexpr size_t PciRegBar5 = 9;
    constexpr size_t PciRegCisPtr = 10;
    constexpr size_t PciRegSubsystemId = 11;
    constexpr size_t PciRegExRom = 12;
    constexpr size_t PciRegCapsPtr = 13;
    constexpr size_t PciRegInterrupts = 15;

    struct PciBar
    {
        uint64_t address;
        size_t size;
        bool isMemory;
        bool isPrefetchable;
        bool is64BitWide;
    };

    /*
        Represents the address of a pci endpoint (segment, bus, device, function). Currently it supports both ecam
        and legacy io port access. It uses a hack of setting the upper 32 bits of the address to detect if its a legacy
        address or not. This is fine for higher half, unless you have mmio stuff mapped up near the kernel binary.
    */
    struct PciAddress
    {
        uint64_t addr;

        FORCE_INLINE static PciAddress CreateLegacy(uint8_t bus, uint8_t device, uint8_t function, uint8_t regOffset)
        {
            //We set the upper 32 bits to indicate this is a legacy address.
            //This region in virtual memory is occupied by the kernel, so it'd cause other issues if an ecam address was in this range.
            return (bus << 16) | ((device & 0b11111) << 11) | ((function & 0b111) << 8) | (regOffset & 0b1111'1100) | (0xFFFF'FFFFul << 32);
        }
        
        FORCE_INLINE static PciAddress CreateEcam(NativeUInt segmentBase, uint8_t bus, uint8_t device, uint8_t function, uint16_t regOffset)
        {
            NativeUInt addend = (bus << 20) | (device << 15) | (function << 12) | (regOffset & 0xFFC);
            return segmentBase + addend;
        }

        PciAddress() : addr(0)
        {}

        PciAddress(NativeUInt address) : addr(address)
        {}

        uint32_t ReadReg(size_t index) const;
        void WriteReg(size_t index, uint32_t data) const;

        FORCE_INLINE bool IsLegacy() const
        { return (addr >> 32) == 0xFFFF'FFFF; }

        PciBar ReadBar(size_t index) const;
    };
}
