#pragma once

#include <drivers/api/Api.h>
#include <PciAddress.h>
#include <VmObject.h>

namespace QemuVga
{
    enum class DispiReg : uint16_t
    {
        Id = 0,
        XRes = 1,
        YRes = 2,
        Bpp = 3,
        Enable = 4,
        Bank = 5,
        VirtWidth = 6,
        VirtHeight = 7,
        XOffset = 8,
        YOffset = 9,
    };

    class GraphicsAdaptor
    {
    private:
        dl::PciAddress pciAddr;
        dl::VmObject framebuffer;
        dl::VmObject mmio;

        void WriteDispiReg(DispiReg reg, uint16_t data) const;
        uint16_t ReadDispiReg(DispiReg reg) const;

    public:
        bool Init(const npk_init_tag_pci_function* pciTag);
    };
}
