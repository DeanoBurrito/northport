#include <GraphicsAdaptor.h>
#include <Log.h>
#include <UnitConverter.h>
#include <drivers/api/Drivers.h>

namespace QemuVga
{
    npk_framebuffer_mode GetModeWrapper(npk_device_api* api)
    {
        auto gfx = static_cast<GraphicsAdaptor*>(api->driver_data);
        return gfx->GetMode();
    }

    npk_string GetSummaryWrapper(npk_device_api* api)
    {
        auto gfx = static_cast<GraphicsAdaptor*>(api->driver_data);
        return gfx->GetSummary();
    }

    constexpr uint16_t DispiDisable = 0x0;
    constexpr uint16_t DispiEnable = 0x1;
    constexpr uint16_t DispiLfbEnabled = 0x40;
    constexpr uint16_t DispiNoClearMem = 0x80;

    constexpr const char builtinSummary[] = "qemu/bochs extended framebuffer";

    void GraphicsAdaptor::WriteDispiReg(DispiReg reg, uint16_t data) const
    {
        mmio->Offset(0x500).Offset((uint16_t)reg << 1).Write(data);
    }

    uint16_t GraphicsAdaptor::ReadDispiReg(DispiReg reg) const
    {
        return mmio->Offset(0x500).Offset((uint16_t)reg << 1).Read<uint16_t>();
    }

    void GraphicsAdaptor::RegenSummary()
    {
        sl::ScopedLock scopeLock(metadataLock);

        //if (summaryString != nullptr)
        //   delete[] summaryString;

        summaryString.length = sizeof(builtinSummary);
        summaryString.data = builtinSummary;
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

        //collect mode info and stash it.
        WriteDispiReg(DispiReg::Enable, DispiDisable);
        mode.width = ReadDispiReg(DispiReg::XRes);
        mode.height = ReadDispiReg(DispiReg::YRes);
        mode.bpp = ReadDispiReg(DispiReg::Bpp);
        WriteDispiReg(DispiReg::Enable, DispiEnable | DispiLfbEnabled | DispiNoClearMem);

        mode.stride = mode.width * (mode.bpp / 8);
        mode.mask_a = mode.shift_a = 0;
        mode.mask_r = mode.mask_g = mode.mask_b = 0xFF;
        mode.shift_r = 0;
        mode.shift_g = 8;
        mode.shift_b = 16;

        RegenSummary();

        //create a device api so the kernel knows about this framebuffer.
        npk_framebuffer_device_api* fbApi = new npk_framebuffer_device_api();
        fbApi->header.type = npk_device_api_type::Framebuffer;
        fbApi->header.driver_data = this;
        fbApi->get_mode = GetModeWrapper;
        fbApi->header.get_summary = GetSummaryWrapper;
        VALIDATE_(npk_add_device_api(&fbApi->header), false);

        return true;
    }
}
