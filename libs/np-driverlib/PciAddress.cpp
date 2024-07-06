#include <PciAddress.h>
#include <Log.h>
#include <interfaces/driver/Io.h>

namespace dl
{
    void PciAddress::Write(size_t offset, uint32_t value) const
    {
        npk_iop_beginning iop {};
        iop.type = npk_iop_type::Write;
        iop.device_api_id = apiId;
        iop.length = sizeof(uint32_t);
        iop.buffer = &value;
        iop.addr = offset;

        VALIDATE_(npk_end_iop(npk_begin_iop(&iop)), );
    }

    uint32_t PciAddress::Read(size_t offset) const
    {
        uint32_t value = 0;

        npk_iop_beginning iop {};
        iop.type = npk_iop_type::Read;
        iop.device_api_id = apiId;
        iop.length = sizeof(uint32_t);
        iop.buffer = &value;
        iop.addr = offset;

        VALIDATE_(npk_end_iop(npk_begin_iop(&iop)), 0);
        return value;
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

            uint64_t upperSize = 0xFFFF'FFFF;
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
