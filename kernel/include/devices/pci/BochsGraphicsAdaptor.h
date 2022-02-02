#pragma once

#include <drivers/DriverManifest.h>
#include <devices/PciBridge.h>
#include <devices/interfaces/GenericGraphicsAdaptor.h>

namespace Kernel::Devices::Pci
{
    namespace BochsGraphics
    {
        void* InitNew(Drivers::DriverInitInfo* initInfo);
        void Destroy(void* inst);
        void HandleEvent(void* inst, Drivers::DriverEventType type, void* arg);
    }

    class BochsGraphicsAdaptor;
    class BochsGraphicsDriver
    {
    private:
        BochsGraphicsAdaptor* adaptor;

    public:
        BochsGraphicsDriver(Drivers::DriverInitInfo* info);
        ~BochsGraphicsDriver();

        BochsGraphicsDriver(const BochsGraphicsDriver& other) = delete;
        BochsGraphicsDriver& operator=(const BochsGraphicsDriver& other) = delete;
        BochsGraphicsDriver(BochsGraphicsDriver&& from) = delete;
        BochsGraphicsDriver& operator=(BochsGraphicsDriver&& from) = delete;

        void HandleEvent(Drivers::DriverEventType type, void* arg);
    };
    
    class BochsFramebuffer : public Interfaces::GenericFramebuffer
    {
    private:
        char lock;

        sl::NativePtr linearFramebufferBase;
        sl::NativePtr mmioBase;
        size_t width;
        size_t height;
        size_t bpp;
        Interfaces::ColourFormat format;

        void WriteVgaReg(uint16_t reg, uint16_t data) const;
        uint16_t ReadVgaReg(uint16_t reg) const;
        void WriteDispiReg(uint16_t reg, uint16_t data) const;
        uint16_t ReadDispiReg(uint16_t reg) const;

    public:
        void Init() override;
        void Deinit() override;
        void Reset() override;
        sl::Opt<void*> GetDriverInstance() override;

        bool CanModeset() const override;
        void SetMode(Interfaces::FramebufferModeset& modeset) override;
        Interfaces::FramebufferModeset GetCurrentMode() const override;

        sl::Opt<sl::NativePtr> GetAddress() const override;
    };

    class BochsGraphicsAdaptor : public Interfaces::GenericGraphicsAdaptor
    {
    private:
        BochsFramebuffer* framebuffer;
        
        void Init() override;
        void Deinit() override;

    public:
        void Reset() override;
        sl::Opt<void*> GetDriverInstance() override;

        size_t GetFramebuffersCount() const override;
        Interfaces::GenericFramebuffer* GetFramebuffer(size_t index) const override;
    };
}
