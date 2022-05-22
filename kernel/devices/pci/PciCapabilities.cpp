#include <devices/pci/PciCapabilities.h>
#include <devices/PciBridge.h>

namespace Kernel::Devices::Pci
{
    sl::Opt<PciCap*> FindPciCap(PciAddress addr, uint8_t withId, PciCap* start)
    {
        uint16_t statusReg = addr.ReadReg(1) >> 16;
        if ((statusReg & (1 << 4)) == 0)
            return {}; //capabilities list not available

        PciCap* cap = EnsureHigherHalfAddr(sl::NativePtr(addr.addr).As<PciCap>(addr.ReadReg(0xD) & 0xFF));
        bool returnNextMatch = (start == nullptr);
        while ((uint64_t)cap != EnsureHigherHalfAddr(addr.addr))
        {
            if (cap->capabilityId == withId && returnNextMatch)
                return cap;
            
            if (cap == start)
                returnNextMatch = true;

            cap = EnsureHigherHalfAddr(sl::NativePtr(addr.addr).As<PciCap>(cap->nextOffset));
        }

        return {};
    }
    
    bool PciCapMsi::Enabled() const
    {
        const uint16_t control = sl::MemRead<uint16_t>((uintptr_t)this + 2);
        return control & 0b1;
    }

    void PciCapMsi::Enable(bool yes)
    {
        const uint16_t control = sl::MemRead<uint16_t>((uintptr_t)this + 2);
        sl::MemWrite<uint16_t>((uintptr_t)this + 2, control | 0b1);
    }

    size_t PciCapMsi::VectorsRequested() const
    {
        const uint16_t control = sl::MemRead<uint16_t>((uintptr_t)this + 2);
        return 1 << ((control >> 1) & 0b111);
    }

    void PciCapMsi::SetVectorsEnabled(size_t count)
    {
        uint16_t control = sl::MemRead<uint16_t>((uintptr_t)this + 2);
        control &= 0xFF8F;

        switch (count)
        {
            case 1:  control |= (0b000) << 4; break;
            case 2:  control |= (0b001) << 4; break;
            case 4:  control |= (0b010) << 4; break;
            case 8:  control |= (0b011) << 4; break;
            case 16: control |= (0b100) << 4; break;
            case 32: control |= (0b101) << 4; break;
            
            default:
                return;
        }
        sl::MemWrite<uint16_t>((uintptr_t)this + 2, control);
    }

    bool PciCapMsi::Has64BitAddress() const
    {
        const uint16_t control = sl::MemRead<uint16_t>((uintptr_t)this + 2);
        return control & (1 << 7);
    }

    void PciCapMsi::SetAddress(sl::NativePtr ptr)
    {
        sl::MemWrite<uint32_t>((uintptr_t)this + 4, ptr.raw & 0xFFFF'FFFF);
        if (Has64BitAddress())
            sl::MemWrite<uint32_t>((uintptr_t)this + 8, ptr.raw >> 32);
    }

    void PciCapMsi::SetData(uint16_t data)
    {
        if (Has64BitAddress())
            sl::MemWrite((uintptr_t)this + 0xC, data);
        else
            sl::MemWrite((uintptr_t)this + 8, data);
    }
    
    bool PciCapMsi::Masked(size_t index) const
    {
        const uint16_t control = sl::MemRead<uint16_t>((uintptr_t)this + 2);
        if ((control & (1 << 8)) == 0)
            return false;
        
        if (index >= 32)
            return false;
        
        if (Has64BitAddress())
            return sl::MemRead<uint32_t>((uintptr_t)this + 0x10) & (1 << index);
        else
            return sl::MemRead<uint32_t>((uintptr_t)this + 0xC) & (1 << index);
    }

    void PciCapMsi::Mask(size_t index, bool masked)
    {
        const uint16_t control = sl::MemRead<uint16_t>((uintptr_t)this + 2);
        if ((control & (1 << 8)) == 0)
            return;
        
        if (index >= 32)
            return;

        const uintptr_t addr = (uintptr_t)this + (Has64BitAddress() ? 0x10 : 0xC);
        uint32_t value = sl::MemRead<uint32_t>(addr) & ~(1 << index);
        if (masked)
            value |= 1 << index;
        sl::MemWrite<uint32_t>(addr, value);
    }

    bool PciCapMsi::Pending(size_t index) const
    {
        const uint16_t control = sl::MemRead<uint16_t>((uintptr_t)this + 2);
        if ((control & (1 << 8)) == 0)
            return false;
        
        if (index >= 32)
            return false;
        
        if (Has64BitAddress())
            return sl::MemRead<uint32_t>((uintptr_t)this + 0x14) & (1 << index);
        else
            return sl::MemRead<uint32_t>((uintptr_t)this + 0x10) & (1 << index);
    }

    sl::NativePtr PciCapMsiX::GetTableEntry(size_t index, PciBar* bars) const
    {
        const size_t count = (sl::MemRead<uint32_t>((uintptr_t)this + 2) & 0x3FF) + 1;
        if (index >= count)
            return nullptr;

        const size_t bir = sl::MemRead<uint32_t>((uintptr_t)this + 4) & 0b111;
        const uintptr_t tableOffset = sl::MemRead<uint32_t>((uintptr_t)this + 4) & ~0b111;
        return bars[bir].address + tableOffset + index * 16;
    }

    bool PciCapMsiX::Enabled() const
    {
        return sl::MemRead<uint32_t>((uintptr_t)this + 2) & (1 << 15);
    }

    void PciCapMsiX::Enable(bool yes)
    {
        uint16_t control = sl::MemRead<uint16_t>((uintptr_t)this + 2);
        control &= 0x7F;
        if (yes)
            control |= 1 << 15;
        sl::MemWrite((uintptr_t)this + 2, control);
    }

    size_t PciCapMsiX::Vectors() const
    {
        return (sl::MemRead<uint32_t>((uintptr_t)this + 2) & 0x3FF) + 1;
    }

    void PciCapMsiX::SetVector(size_t index, uint64_t address, uint16_t data, PciBar* bars)
    {
        sl::NativePtr entryAddr = GetTableEntry(index, bars);
        if (entryAddr.ptr == nullptr)
            return;

        sl::MemWrite<uint64_t>(entryAddr, address);
        sl::MemWrite<uint32_t>(entryAddr.raw + 8, data);
    }

    bool PciCapMsiX::Masked(size_t index, PciBar* bars) const
    {
        sl::NativePtr entryAddr = GetTableEntry(index, bars);
        if (entryAddr.ptr == nullptr)
            return false;

        return sl::MemRead<uint16_t>(entryAddr.raw + 12) & 0b1;
    }

    void PciCapMsiX::Mask(size_t index, bool masked, PciBar* bars)
    {
        sl::NativePtr entryAddr = GetTableEntry(index, bars);
        if (entryAddr.ptr == nullptr)
            return;
        
        const uint16_t value = sl::MemRead<uint16_t>(entryAddr.raw + 12);
        sl::MemWrite(entryAddr.raw + 12, value | 0b1);
    }

    bool PciCapMsiX::Pending(size_t index, PciBar* bars) const
    {
         const size_t count = (sl::MemRead<uint32_t>((uintptr_t)this + 2) & 0x3FF) + 1;
        if (index >= count)
            return false;

        const size_t bir = sl::MemRead<uint32_t>((uintptr_t)this + 4) & 0b111;
        const uintptr_t arrayOffset = sl::MemRead<uint32_t>((uintptr_t)this + 4) & ~0b111;
        const sl::NativePtr arrayAddr = bars[bir].address + arrayOffset;

        return (sl::MemRead<uint64_t>(arrayAddr) + index / 64) & (1 << index % 64);
    }
}
