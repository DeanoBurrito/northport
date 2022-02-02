#pragma once

#include <stddef.h>
#include <IdAllocator.h>
#include <containers/Vector.h>
#include <devices/GenericDevice.h>
#include <devices/StivaleFramebuffer.h>

namespace Kernel::Devices
{
    class DeviceManager
    {
    private:
        sl::UIdAllocator* idAlloc;
        sl::Vector<GenericDevice*>* allDevices;
        GenericDevice* primaryDevices[(size_t)DeviceType::EnumCount];

        char allDevicesLock;
        char primaryDevicesLock;

        StivaleFramebuffer* bootloaderFramebuffer;

    public:
        static DeviceManager* Global();

        void Init();
        size_t RegisterDevice(GenericDevice* device);
        [[nodiscard]]
        GenericDevice* UnregisterDevice(size_t deviceId);
        sl::Opt<GenericDevice*> GetDevice(size_t deviceId);
        void ResetDevice(size_t deviceId);

        sl::Vector<size_t> FindDevicesOfType(DeviceType type);
        sl::Opt<GenericDevice*> GetPrimaryDevice(DeviceType type) const;
        void SetPrimaryDevice(DeviceType type, size_t deviceId);
    };
}
