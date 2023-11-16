#include <PciAddress.h>
#include <Log.h>
#include <NativePtr.h>
#include <drivers/api/Memory.h>
#include <drivers/api/Api.h>

#ifdef __x86_64__
    #define DL_LEGACY_PCI_SUPPORT
#endif

namespace dl
{
    constexpr uintptr_t PciLegacyPoison = 0x7777'7777;

    PciAddress PciAddress::FromEcam(uintptr_t segmentBase, uint8_t bus, uint8_t device, uint8_t func)
    {
        const uintptr_t physAddr = segmentBase + ((bus << 20) | (device << 15) | (func << 12));

        //TODO: freeing of config space address
        auto addr = npk_vm_alloc(0x1000, (void*)physAddr, static_cast<npk_vm_flags>(VmMmio | VmWrite), nullptr);
        VALIDATE_(addr != nullptr, {});
        return PciAddress(reinterpret_cast<uintptr_t>(addr));
    }

    PciAddress PciAddress::FromLegacy(uint8_t bus, uint8_t device, uint8_t func)
    {
        const uintptr_t addr = (PciLegacyPoison << 32) | (bus << 16) | (device << 11) | (func << 8);
        return PciAddress(addr);
    }

    void PciAddress::Write(size_t offset, uint32_t value) const
    {
#ifdef DL_LEGACY_PCI_SUPPORT
        if ((addr >> 32) == PciLegacyPoison)
            return npk_pci_legacy_write32((addr & 0xFFFF'FFFF) | (offset & 0xFF), value);
#endif
        sl::NativePtr(addr).Offset(offset & 0xFFF).Write<uint32_t>(value);
    }

    uint32_t PciAddress::Read(size_t offset) const
    {
#ifdef DL_LEGACY_PCI_SUPPORT
        if ((addr >> 32) == PciLegacyPoison)
            return npk_pci_legacy_read32((addr & 0xFFFF'FFFF) | (offset & 0xFF));
#endif
        return sl::NativePtr(addr).Offset(offset & 0xFFF).Read<uint32_t>();
    }

    PciBar PciAddress::ReadBar(uint8_t index, bool noSize) const
    {
        constexpr size_t Bar0Index = 4;

        if (index > 5)
            return {}; //bad index

        PciBar bar {};
        bar.index = index;

        index += Bar0Index;
        const uint32_t originalLow = ReadReg(index);

        if (originalLow & 0b1)
        {
            //IO BAR
            bar.isMemory = bar.is64bit = bar.isPrefetchable = false;
            bar.base = originalLow & ~(uint32_t)0b11;
            //TODO: account for pci-to-device address space translations (like device tree ranges property)

            if (!noSize)
            {
                WriteReg(index, 0xFFFF'FFFF);
                const uint32_t readback = ReadReg(index);
                bar.length = ~(readback & 0b11) + 1;
                WriteReg(index, originalLow);
            }
        }
        else
        {
            //memory BAR
            bar.isMemory = true;
            bar.is64bit = originalLow & (1 << 2);
            bar.isPrefetchable = originalLow & (1 << 3);
            bar.base = originalLow & ~(uint32_t)0xF;

            uintptr_t upperSize = 0xFFFF'FFFF;
            if (bar.is64bit)
            {
                bar.base |= ((uint64_t)ReadReg(index + 1)) << 32;

                if (!noSize)
                {
                    const uint32_t originalHigh = ReadReg(index + 1);
                    WriteReg(index + 1, 0xFFFF'FFFF);
                    upperSize = ReadReg(index + 1);
                    WriteReg(index + 1, originalHigh);
                }
            }
            //TODO: again, account for pci to host address space translations

            if (!noSize)
            {
                WriteReg(index, 0xFFFF'FFFF);
                bar.length = (upperSize << 32) | (ReadReg(index) & ~(uint32_t)0xF);
                WriteReg(index, originalLow);
                bar.length = (~bar.length) + 1;
            }
        }

        if (noSize)
            bar.length = 0;
        return bar;
    }

    bool PciAddress::BitRw(size_t reg, size_t index, sl::Opt<bool> set) const
    {
        uint32_t regValue = ReadReg(reg);
        const bool prevState = regValue & (1 << index);

        if (set.HasValue())
        {
            regValue = *set ? (regValue | (1 << index)) : (regValue & ~((uint32_t)1 << index));
            WriteReg(reg, regValue);
        }
        return prevState;
    }
}
