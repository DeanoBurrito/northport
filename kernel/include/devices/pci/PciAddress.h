#pragma once

#include <stdint.h>
#include <Platform.h>

namespace Kernel::Devices::Pci
{
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

        uint32_t ReadReg(size_t index);
        void WriteReg(size_t index, uint32_t data);
        FORCE_INLINE bool IsLegacy() const
        { return (addr >> 32) == 0xFFFF'FFFF; }
    };
}
