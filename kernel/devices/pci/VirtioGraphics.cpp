#include <devices/pci/VirtioGraphics.h>
#include <devices/pci/VirtioQueue.h>
#include <devices/DeviceManager.h>
#include <devices/PciBridge.h>
#include <Memory.h>
#include <Locks.h>
#include <Log.h>

namespace Kernel::Devices::Pci
{
    void VirtioGraphicsDriver::Init(Drivers::DriverInitInfo* initInfo)
    {
        auto maybeAddr = initInfo->FindTag(Drivers::DriverInitTagType::PciFunction);
        if (!maybeAddr)
        {
            Log("Attempted to start VirtIO gpu driver, but no pci address was provided. Aborting init.", LogSeverity::Error);
            return;
        }

        Drivers::DriverInitTagPci* pciTag = reinterpret_cast<Drivers::DriverInitTagPci*>(*maybeAddr);
        gpu = new VirtioGpu(this, pciTag->address);
        DeviceManager::Global()->RegisterDevice(gpu);

        if (gpu->GetState() != DeviceState::Ready)
        {
            delete DeviceManager::Global()->UnregisterDevice(gpu->GetId());
            gpu = nullptr;
            return;
        }
    }

    void VirtioGraphicsDriver::Deinit()
    {
        delete DeviceManager::Global()->UnregisterDevice(gpu->GetId());
        gpu = nullptr;
    }

    void VirtioGraphicsDriver::HandleEvent(Drivers::DriverEventType, void*)
    {}

    void VirtioGpu::Init()
    {
        sl::ScopedSpinlock scopeLock(&lock);

        if (state == DeviceState::Ready)
            return;

        if (pciDevice.addr == 0)
        {
            Log("Cannot initialize virtio gpu device, invalid pci address.", LogSeverity::Error);
            state = DeviceState::Error;
            return;
        }

        if (!PciBridge::Global()->EcamAvailable())
        {
            Log("Cannot initialize virtio gpu device, PCI ECAM not available.", LogSeverity::Error);
            state = DeviceState::Error;
            return;
        }

        auto maybeCfg = GetVirtioPciCommonConfig(pciDevice);
        if (!maybeCfg)
        {
            Log("Cannot initialize virtio gpu device, could not locate common config registers.", LogSeverity::Error);
            state = DeviceState::Error;
            return;
        }
        volatile VirtioPciCommonConfig* commonConfig = *maybeCfg;

        //as per spec, lets fully reset the device, set the acknowledge and driver bits.
        commonConfig->deviceStatus = 0;
        commonConfig->deviceStatus = (uint8_t)DeviceStatus::Acknowledge | (uint8_t)DeviceStatus::Driver;

        //TODO: actually enumerate gpu features, and select what we need instead of 'accept all'.
        commonConfig->driverFeature = commonConfig->deviceFeature;
        commonConfig->deviceStatus = 0b1011;

        queues = VirtioQueue::FindQueues(*maybeCfg);
        if (queues.Size() != 2)
        {
            Log("Unexpected number of queues for virtio gpu, aborting device init.", LogSeverity::Error);
            state = DeviceState::Error;
            return;
        }

        //TODO: allocate buffers for virtqueues, communicate with gpu
        
        state = DeviceState::Ready;
    }

    void VirtioGpu::Deinit()
    {
        sl::ScopedSpinlock scopeLock(&lock);
        
        state = DeviceState::Shutdown;
    }

    void VirtioGpu::Reset()
    {
        Deinit();
        Init();
    }

    sl::Opt<Drivers::GenericDriver*> VirtioGpu::GetDriverInstance()
    { return owner; }

    size_t VirtioGpu::GetFramebuffersCount() const
    {}

    Interfaces::GenericFramebuffer* VirtioGpu::GetFramebuffer(size_t index) const
    {}

    void VirtioFramebuffer::Init()
    {
        sl::ScopedSpinlock scopeLock(&lock);

        if (state == DeviceState::Ready)
            return;

        state = DeviceState::Ready;
    }

    void VirtioFramebuffer::Deinit()
    {
        sl::ScopedSpinlock scopeLock(&lock);
        state = DeviceState::Shutdown;
    }

    void VirtioFramebuffer::Reset()
    {
        Deinit();
        Init();
    }

    sl::Opt<Drivers::GenericDriver*> VirtioFramebuffer::GetDriverInstance()
    { return owner->GetDriverInstance(); }

    bool VirtioFramebuffer::CanModeset() const
    { return true; }

    void VirtioFramebuffer::SetMode(Interfaces::FramebufferModeset& modeset)
    {}

    Interfaces::FramebufferModeset VirtioFramebuffer::GetCurrentMode() const
    {}

    sl::Opt<sl::NativePtr> VirtioFramebuffer::GetAddress() const
    { return {}; }

    Drivers::GenericDriver* CreateNewVirtioGpuDriver()
    { return new VirtioGraphicsDriver(); }
}
