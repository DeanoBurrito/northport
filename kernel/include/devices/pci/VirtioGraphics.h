#pragma once

#include <drivers/GenericDriver.h>
#include <devices/interfaces/GenericGraphicsAdaptor.h>
#include <devices/pci/PciAddress.h>
#include <devices/pci/VirtioQueue.h>

namespace Kernel::Devices::Pci
{
    enum class VirtioGpuFeatures : uint64_t
    {
        None = 0,

        Virgl3D = (1 << 0),
        EdidSuported = (1 << 1),
    };

    //device config for virtio gpu
    struct VirtioGpuConfig
    {
        uint32_t eventsRead;
        uint32_t eventsClear;
        uint32_t scanouts;
        uint32_t reserved;
    };
    
    class VirtioGpu;
    class VirtioFramebuffer;
    
    class VirtioGraphicsDriver : public Drivers::GenericDriver
    {
    private:
        VirtioGpu* gpu;

    public:
        void Init(Drivers::DriverInitInfo* initInfo) override;
        void Deinit() override;
        void HandleEvent(Drivers::DriverEventType type, void* eventArgs) override;
    };

    class VirtioGpu : public GenericGraphicsAdaptor
    {
    friend VirtioFramebuffer;
    private:
        VirtioGraphicsDriver* owner;
        PciAddress pciDevice;
        sl::Vector<VirtioQueue> queues;
        sl::Vector<VirtioFramebuffer*> scanouts;

    protected:
        void Init() override;
        void Deinit() override;

    public:
        VirtioGpu(VirtioGraphicsDriver* parent, PciAddress addr) : owner(parent), pciDevice(addr)
        {}

        void Reset() override;
        sl::Opt<Drivers::GenericDriver*> GetDriverInstance() override;
        size_t GetFramebuffersCount() const override;
        GenericFramebuffer* GetFramebuffer(size_t index) const override;
    };

    class VirtioFramebuffer : public GenericFramebuffer
    {
    friend VirtioGpu;
    private:
        VirtioGpu* owner;

    protected:
        void Init() override;
        void Deinit() override;

    public:
        void Reset() override;
        sl::Opt<Drivers::GenericDriver*> GetDriverInstance() override;
        
        bool CanModeset() const override;
        void SetMode(FramebufferModeset& modeset) override;
        FramebufferModeset GetCurrentMode() const override;
        sl::Opt<sl::NativePtr> GetAddress() const override;
    };

    Drivers::GenericDriver* CreateNewVirtioGpuDriver();
}
