#include <devices/PciAddress.h>
#include <devices/PciBridge.h>
#include <arch/Platform.h>
#include <debug/Log.h>
#include <memory/Vmm.h>
#include <NativePtr.h>

namespace Npk::Devices
{
#ifdef __x86_64__
    constexpr uint16_t PortPciAddr = 0xCF8;
    constexpr uint16_t PortPciData = 0xCFC;
    constexpr uint32_t PciEnableConfig = 0x80000000;
#endif
    constexpr uintptr_t PciLegacyPoison = 0x8888'8888;

    PciAddress PciAddress::CreateEcam(uintptr_t segmentBase, uint8_t bus, uint8_t device, uint8_t function)
    {
        const uintptr_t physAddr = segmentBase + ((bus << 20) | (device << 15) | (function << 12));
        
        //NOTE: this incurs an overhead of allocating virtual address space with every PCI address
        //discovered by the kernel system, space which is never released. I think this is fine as these objects
        //are passed around quite frequently, but it may be worth looking into in the future.
        //It's also worth noting that pci addreses are only created like this when a device is initially discovered.
        auto accessWindow = VMM::Kernel().Alloc(0x1000, physAddr, VmFlags::Mmio | VmFlags::Write);
        ASSERT(accessWindow, "VMM::Alloc()");
        return accessWindow->base;
    }

    PciAddress PciAddress::CreateLegacy(uint8_t bus, uint8_t device, uint8_t function)
    {
        return (PciLegacyPoison << 32) | (bus << 16) | (device << 11) | (function << 8);
    }

    void PciAddress::WriteAt(size_t offset, uint32_t value) const
    {
#ifdef __x86_64__
        if (IsLegacy())
        {
            Out32(PortPciAddr, (uint32_t)addr | PciEnableConfig | (offset & 0xFF));
            Out32(PortPciData, value);
        }
#endif
        sl::NativePtr(addr).Offset(offset & 0xFFF).Write<uint32_t>(value);
    }

    uint32_t PciAddress::ReadAt(size_t offset) const
    {
#ifdef __x86_64__
        if (IsLegacy())
        {
            Out32(PortPciAddr, (uint32_t)addr | PciEnableConfig | (offset & 0xFF));
            return In32(PortPciData);
        }
#endif
        return sl::NativePtr(addr).Offset(offset & 0xFFF).Read<uint32_t>();
    }

    bool PciAddress::IsLegacy() const
    {
#ifndef __x86_64__
        return false;
#else
        return (addr >> 32) == PciLegacyPoison;
#endif
    }

    PciBar PciAddress::ReadBar(size_t index, bool noSize) const
    {
        ASSERT(index < 6, "Invalid PCI BAR");

        PciBar bar;
        const size_t offset = ((size_t)PciReg::Bar0 + index) * 4;
        const uint32_t original = ReadAt(offset);

        if (original & 0b1)
        {
            //io bar
            bar.isMemory = bar.is64Bit = bar.isPrefetchable = false;
            bar.address = original & ~(uint32_t)0b11;
            bar.address = *PciBridge::Global().PciToHost(bar.address, PciSpaceType::BarIo);
            
            if (!noSize)
            {
                WriteAt(offset, 0xFFFF'FFFF);
                const uint32_t readback = ReadAt(offset);
                bar.size = ~(readback & 0b11) + 1;
                WriteAt(offset, original);
            }
        }
        else
        {
            //memory bar
            bar.isMemory = true;
            bar.is64Bit = original & (1 << 2);
            bar.isPrefetchable = original & (1 << 3);
            bar.address = original & ~(uint32_t)0xF;

            uintptr_t upperSize = 0xFFFF'FFFF;

            if (bar.is64Bit)
            {
                bar.address |= (uint64_t)ReadAt(offset + 4) << 32;
                bar.address = *PciBridge::Global().PciToHost(bar.address, PciSpaceType::Bar64);

                if (!noSize)
                {
                    const uint32_t originalUpper = ReadAt(offset + 4);
                    WriteAt(offset + 4, 0xFFFF'FFFF);
                    upperSize = ReadAt(offset + 4);
                    WriteAt(offset + 4, originalUpper);
                }
            }
            else
                bar.address = *PciBridge::Global().PciToHost(bar.address, PciSpaceType::Bar32);

            if (!noSize)
            {
                WriteAt(offset, 0xFFFF'FFFF);
                bar.size = upperSize << 32 | (ReadAt(offset) & ~(uint32_t)0xF);
                WriteAt(offset, original);

                bar.size = (~bar.size) + 1;
            }
        }

        if (noSize)
            bar.size = 0;
        bar.index = index;
        return bar;
    }

    bool PciAddress::BitReadWrite(PciReg reg, size_t index, sl::Opt<bool> setValue) const
    {
        uint32_t regValue = ReadReg(reg);
        const bool prevState = regValue & (1 << index);

        if (setValue)
        {
            regValue = *setValue ? regValue | (1 << index) : regValue & ~((uint32_t)1 << index);
            WriteReg(reg, regValue);
        }
        return prevState;
    }
}
