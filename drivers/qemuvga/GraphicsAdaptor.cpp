#include <GraphicsAdaptor.h>
#include <Log.h>
#include <UnitConverter.h>

namespace QemuVga
{
    constexpr uint16_t DispiDisable = 0x0;
    constexpr uint16_t DispiEnable = 0x1;
    constexpr uint16_t DispiLfbEnabled = 0x40;
    constexpr uint16_t DispiNoClearMem = 0x80;

    void GraphicsAdaptor::WriteDispiReg(DispiReg reg, uint16_t data) const
    {
        mmio->Offset(0x500).Offset((uint16_t)reg << 1).Write(data);
    }

    uint16_t GraphicsAdaptor::ReadDispiReg(DispiReg reg) const
    {
        return mmio->Offset(0x500).Offset((uint16_t)reg << 1).Read<uint16_t>();
    }

    bool GraphicsAdaptor::Init(const npk_init_tag_pci_function* pciTag)
    {
        switch (pciTag->type)
        {
        case npk_pci_addr_type::Ecam:
            pciAddr = dl::PciAddress::FromEcam(pciTag->segment_base, pciTag->bus, 
                pciTag->device, pciTag->function);
            break;
        case npk_pci_addr_type::Legacy:
            pciAddr = dl::PciAddress::FromLegacy(pciTag->bus, pciTag->device, pciTag->function);
            break;
        default:
            Log("Unknown PCI transport: %u", LogLevel::Error, pciTag->type);
            return false;
        }

        Log("Accessed via %s: 0x%lx::%02x:%02x:%01x", LogLevel::Verbose, 
            pciTag->type == npk_pci_addr_type::Ecam ? "ECAM" : "legacy-IO", 
            pciTag->segment_base, pciTag->bus, pciTag->device, pciTag->function);

        pciAddr.MemorySpaceEnable(true);
        //map the framebuffer into virtual memory
        const auto bar0 = pciAddr.ReadBar(0);
        VALIDATE(bar0.length != 0, false, "BAR0 should contain framebuffer");
        framebuffer = dl::VmObject(bar0.length, bar0.base, VmFlag::Mmio | VmFlag::Write);
        VALIDATE_(framebuffer.Valid(), false);

        //map bar2, which contains out qemu/bochs extension registers.
        const auto bar2 = pciAddr.ReadBar(2);
        VALIDATE_(bar2.length != 0, false);
        mmio = dl::VmObject(bar2.length, bar2.base, VmFlag::Mmio | VmFlag::Write);
        VALIDATE_(mmio.Valid(), false);

        WriteDispiReg(DispiReg::Enable, DispiDisable);
        const size_t w = ReadDispiReg(DispiReg::XRes);
        const size_t h = ReadDispiReg(DispiReg::YRes);
        const size_t bpp = ReadDispiReg(DispiReg::Bpp);
        WriteDispiReg(DispiReg::Enable, DispiEnable | DispiLfbEnabled | DispiNoClearMem);

        Log("Bochs/Qemu extended VGA init: %lux%lu, %lu-bpp", LogLevel::Info, w, h, bpp);
        return true;
    }
}
