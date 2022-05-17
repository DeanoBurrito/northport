#include <devices/pci/PciAddress.h>
#include <devices/PciBridge.h>
#include <Memory.h>

namespace Kernel::Devices::Pci
{
    constexpr uint32_t PciEnableConfigCycle = 0x80000000;

    uint32_t PciAddress::ReadReg(size_t index)
    {
        if (addr >> 32 != 0xFFFF'FFFF)
            return sl::MemRead<uint32_t>(EnsureHigherHalfAddr(addr + (index * 4)));
        else
        {
            CPU::PortWrite32(PORT_PCI_CONFIG_ADDRESS, (uint32_t)addr | (PciEnableConfigCycle + (index * 4)));
            return CPU::PortRead32(PORT_PCI_CONFIG_DATA);
        }
    }

    void PciAddress::WriteReg(size_t index, uint32_t data)
    {
        if (addr >> 32 != 0xFFFF'FFFF)
            sl::MemWrite(EnsureHigherHalfAddr(addr + (index * 4)), data);
        else
        {
            CPU::PortWrite32(PORT_PCI_CONFIG_ADDRESS, (uint32_t)addr | (PciEnableConfigCycle + (index * 4)));
            CPU::PortWrite32(PORT_PCI_CONFIG_DATA, data);
        }
    }
}
