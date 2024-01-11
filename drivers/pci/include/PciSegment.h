#pragma once

#include <drivers/api/Api.h>
#include <VmObject.h>

namespace Pci
{
    struct PciBusAccess
    {
        dl::VmObject access;
        uint8_t busId;
    };

    class PciSegment
    {
    private:
        sl::Vector<PciBusAccess> busAccess;
        uintptr_t base;
        uint16_t id;

        void* CalculateAddress(void* busAccess, uint8_t dev, uint8_t func);
        void RegisterDescriptor(void* addr, uint8_t bus, uint8_t dev, uint8_t func);

    public:
        bool Init(const npk_init_tag_pci_host* host);

        void WriteReg(void* addr, size_t reg, uint32_t value);
        uint32_t ReadReg(void* addr, size_t reg);

        [[gnu::always_inline]]
        inline uint16_t Id() const
        { return id; }
    };
}
