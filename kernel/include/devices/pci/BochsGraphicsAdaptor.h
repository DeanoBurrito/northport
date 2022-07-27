#pragma once

#include <drivers/GenericDriver.h>
#include <devices/interfaces/GenericGraphicsAdaptor.h>

namespace Kernel::Devices::Pci
{
    Drivers::GenericDriver* CreateNewBgaDriver();
    
    class BochsGraphicsAdaptor;
    class BochsGraphicsDriver : public Drivers::GenericDriver
    {
    private:
        BochsGraphicsAdaptor* adaptor;

    public:
        void Init(Drivers::DriverInitInfo* initInfo) override;
        void Deinit() override;
        void HandleEvent(Drivers::DriverEventType type, void* arg) override;
    };
    
    class BochsFramebuffer : public GenericFramebuffer
    {
    private:
        char lock;

        sl::NativePtr linearFramebufferBase;
        sl::NativePtr mmioBase;
        size_t width;
        size_t height;
        size_t bpp;
        ColourFormat format;

        void WriteVgaReg(uint16_t reg, uint16_t data) const;
        uint16_t ReadVgaReg(uint16_t reg) const;
        void WriteDispiReg(uint16_t reg, uint16_t data) const;
        uint16_t ReadDispiReg(uint16_t reg) const;

    public:
        void Init() override;
        void Deinit() override;
        void Reset() override;
        sl::Opt<Drivers::GenericDriver*> GetDriverInstance() override;

        bool CanModeset() const override;
        void SetMode(FramebufferModeset& modeset) override;
        FramebufferModeset GetCurrentMode() const override;

        sl::Opt<sl::NativePtr> GetAddress() const override;
    };

    class BochsGraphicsAdaptor : public GenericGraphicsAdaptor
    {
    private:
        BochsFramebuffer* framebuffer;
        
        void Init() override;
        void Deinit() override;

    public:
        void Reset() override;
        sl::Opt<Drivers::GenericDriver*> GetDriverInstance() override;

        size_t GetFramebuffersCount() const override;
        GenericFramebuffer* GetFramebuffer(size_t index) const override;
    };
}
