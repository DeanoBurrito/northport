#include <devices/pci/PciAddress.h>
#include <Memory.h>

namespace Kernel::Devices::Pci
{
    constexpr uint32_t PciEnableConfigCycle = 0x80000000;

    uint32_t PciAddress::ReadReg(size_t index) const
    {
        if (addr >> 32 != 0xFFFF'FFFF)
            return sl::MemRead<uint32_t>(EnsureHigherHalfAddr(addr + (index * 4)));
        else
        {
            PortWrite32(PORT_PCI_CONFIG_ADDRESS, (uint32_t)addr | (PciEnableConfigCycle + (index * 4)));
            return PortRead32(PORT_PCI_CONFIG_DATA);
        }
    }

    void PciAddress::WriteReg(size_t index, uint32_t data) const
    {
        if (addr >> 32 != 0xFFFF'FFFF)
            sl::MemWrite(EnsureHigherHalfAddr(addr + (index * 4)), data);
        else
        {
            PortWrite32(PORT_PCI_CONFIG_ADDRESS, (uint32_t)addr | (PciEnableConfigCycle + (index * 4)));
            PortWrite32(PORT_PCI_CONFIG_DATA, data);
        }
    }

    PciBar PciAddress::ReadBar(size_t index) const
    {
        PciBar bar;
        const uint32_t original = ReadReg(PciRegBar0 + index);
        if (original & 0b1)
        {
            //io bar
            bar.isMemory = bar.is64BitWide = bar.isPrefetchable = false;
            bar.address = original & (uint32_t)~0b11;

            WriteReg(PciRegBar0 + index, 0xFFFF'FFFF);
            const uint32_t readback = ReadReg(PciRegBar0 + index);
            bar.size = ~(readback & ~0b11) + 1;
            WriteReg(PciRegBar0 + index, original);
        }
        else
        {
            //memory bar
            uint64_t upperSize = 0xFFFF'FFFF;
            bar.isMemory = true;
            bar.is64BitWide = original & 0b100;
            bar.isPrefetchable = original & 0b1000;
            bar.address = original & ~(uint32_t)0b1111;

            if (bar.is64BitWide)
            {
                bar.address |= (uint64_t)ReadReg(PciRegBar1 + index) << 32;
                WriteReg(PciRegBar1 + index, 0xFFFF'FFFF);
                upperSize = ReadReg(PciRegBar1 + index);
                WriteReg(PciRegBar1 + index, bar.address >> 32);
            }
            
            WriteReg(PciRegBar0 + index, 0xFFFF'FFFF);
            bar.size = (upperSize << 32) | (ReadReg(PciRegBar0) & ~0b1111);
            WriteReg(PciRegBar0 + index, original);
            bar.size = ~bar.size + 1;
        }

        return bar;
    }
}
