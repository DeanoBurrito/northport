#pragma once

#include <devices/GenericDevices.h>
#include <devices/PciAddress.h>

namespace Npk::Drivers
{
    void BochsVgaMain(void* arg);

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

    class BochsFramebuffer : public Devices::GenericFramebuffer
    {
        Devices::PciAddress addr;
        sl::NativePtr mmioRegs;
        sl::NativePtr fbBase;

        size_t width;
        size_t height;
        size_t bpp;

        void WriteVgaReg(uint16_t reg, uint16_t data) const;
        uint16_t ReadVgaReg(uint16_t reg) const;
        void WriteDispiReg(DispiReg reg, uint16_t data) const;
        uint16_t ReadDispiReg(DispiReg reg) const;

    public:
        BochsFramebuffer(Devices::PciAddress address) : addr(address)
        {}

        bool Init() override;
        bool Deinit() override;
        bool CanModeset() override;
        Devices::FramebufferMode CurrentMode() override;
        bool SetMode(const Devices::FramebufferMode& newMode) override;
        sl::NativePtr LinearAddress() override;
    };
}
