#include <devices/PciCapabilities.h>
#include <arch/Platform.h>
#include <debug/Log.h>
#include <NativePtr.h>

namespace Npk::Devices
{
    sl::Opt<PciCap> PciCap::Find(PciAddress addr, uint8_t id, sl::Opt<PciCap> start)
    {
        size_t offset = addr.ReadReg(PciReg::CapsPtr) & 0xFF;
        if (start)
            offset = (addr.ReadAt(start->offset) >> 8) & 0xFF;

        while (offset != 0)
        {
            const uint32_t capReg = addr.ReadAt(offset);
            if ((capReg & 0xFF) == id)
                return PciCap{ addr, offset };
            
            offset = (capReg >> 8) & 0xFF;
        }

        return {};
    }

    uint8_t PciCap::Id() const
    {
        return base.ReadAt(offset) & 0xFF;
    }

    uint32_t PciCap::ReadReg(size_t index) const
    {
        return base.ReadAt(index * 4 + offset);
    }

    void PciCap::WriteReg(size_t index, uint32_t value) const
    {
        base.WriteAt(index * 4 + offset, value);
    }

    bool PciCap::BitReadWrite(size_t regIndex, size_t bitIndex, sl::Opt<bool> setValue) const
    {
        uint32_t regValue = ReadReg(regIndex);
        const bool prevState = regValue & (1 << bitIndex);
        
        if (setValue)
        {
            regValue = *setValue ? regValue | (1 << bitIndex) : regValue & ~((uint32_t)1 << bitIndex);
            WriteReg(regIndex, regValue);
        }
        return prevState;
    }

    void MsiCap::SetMessage(uintptr_t addr, uint16_t data) const
    {
        const uint32_t msgControl = cap.ReadReg(0);
        const bool is64Bit = msgControl & (1 << 23);
        const bool vectorMasking = msgControl & (1 << 24);
        
        //to keep things simple we only use a single MSI-message for now.
        cap.WriteReg(0, msgControl & ~(0b111 << 4)); //0b000 means 1 message

        if (is64Bit)
        {
            cap.WriteReg(1, addr);
            cap.WriteReg(2, addr >> 32);
            cap.WriteReg(3, data);
        }
        else
        {
            cap.WriteReg(1, addr);
            cap.WriteReg(2, data);
        }

        if (vectorMasking)
            cap.WriteReg(3 + (is64Bit ? 1 : 0), 0);
    }

    size_t MsixCap::TableSize() const
    {
        return ((cap.ReadReg(1) >> 16) & 0x3FF) + 1;
    }

    void MsixCap::SetEntry(size_t index, uintptr_t addr, uint32_t data, bool masked) const
    {
        const uint32_t bir = cap.ReadReg(1);
        const size_t birOffset = bir & ~0b111u;
        const PciBar bar = cap.base.ReadBar(bir & 0b111);
        ASSERT(bar.isMemory, "BIR must be memory space.");

        sl::NativePtr entry { bar.address + birOffset + hhdmBase + (index * 4) };
        entry.Offset(0).Write<uint32_t>(addr);
        entry.Offset(4).Write<uint32_t>(addr >> 32);
        entry.Offset(8).Write<uint32_t>(data);
        entry.Offset(12).Write<uint32_t>(masked ? 0b1 : 0b0);
    }

    void MsixCap::MaskEntry(size_t index, bool mask) const
    {
        const uint32_t bir = cap.ReadReg(1);
        const size_t birOffset = bir & ~0b111u;
        const PciBar bar = cap.base.ReadBar(bir & 0b111);
        ASSERT(bar.isMemory, "BIR must be memory space.");

        sl::NativePtr entry { bar.address + birOffset + hhdmBase + (index * 4) };
        entry.Offset(12).Write<uint32_t>(mask ? 0b1 : 0b0);
    }

    void MsixCap::GetEntry(size_t index, uintptr_t& addr, uint32_t& data, bool& masked) const
    {
        const uint32_t bir = cap.ReadReg(1);
        const size_t birOffset = bir & ~0b111u;
        const PciBar bar = cap.base.ReadBar(bir & 0b111);
        ASSERT(bar.isMemory, "BIR must be memory space.");

        sl::NativePtr entry { bar.address + birOffset + hhdmBase + (index * 4) };
        addr = entry.Read<uint32_t>();
        addr |= (uint64_t)entry.Offset(4).Read<uint32_t>() << 32;
        data = entry.Offset(8).Read<uint32_t>();
        masked = entry.Offset(12).Read<uint32_t>() & 0b1;
    }
}
