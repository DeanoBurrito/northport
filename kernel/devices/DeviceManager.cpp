#include <devices/DeviceManager.h>
#include <Locks.h>
#include <Log.h>

/*
Notes about graphics BUP:
    After reading serenity source, looks like they default to the bootloader provided framebuffer, if nothing specific can be initialized.
    Would be nice to support that sort of functionality here too.

    We'd probably want to define a set of defices which *require* a primary definiton between we consider device initialization complete.
    For now this would simply be the framebuffer and keyboard I guess (although kb is a 1-way device, so perhaps not necessary).

    Perhaps we have an interface like `DeviceManager::FinishInit()`, which checks for any primary devices, and assigns their defaults (i.e bootloader fb).
*/

namespace Kernel::Devices
{
    DeviceManager deviceManagerGlobal;
    DeviceManager* DeviceManager::Global()
    { return &deviceManagerGlobal; }

    void DeviceManager::Init()
    {
        idAlloc = new sl::UIdAllocator();
        allDevices = new sl::Vector<GenericDevice*>();

        for (size_t i = 0; i < (size_t)DeviceType::EnumCount; ++i)
            primaryDevices[i] = nullptr;
        sl::SpinlockRelease(&allDevicesLock);
        sl::SpinlockRelease(&primaryDevicesLock);
        
        Log("Kernel device manager initialized.", LogSeverity::Info);
    }

    size_t DeviceManager::RegisterDevice(GenericDevice* device)
    {
        if (device == nullptr)
        {
            Log("Cannot register device: given nullptr as device ptr.", LogSeverity::Error);
            return (size_t)-1;
        }

        device->deviceId = idAlloc->Alloc();
        device->state = DeviceState::Initializing;

        //ensure the capacity is big enough (in 1 allocation), and then ensure we have an available slot.
        sl::SpinlockAcquire(&allDevicesLock);
        if (allDevices->Size() <= device->deviceId)
            allDevices->EnsureCapacity(device->deviceId);
        while (allDevices->Size() <= device->deviceId)
            allDevices->EmplaceBack(nullptr);
    
        allDevices->At(device->deviceId) = device;
        sl::SpinlockRelease(&allDevicesLock);

        device->Init();
        const bool initError = (device->state == DeviceState::Error) || (device->state == DeviceState::Unknown);
        Logf("New device registered: id=%u, initError=%b", LogSeverity::Verbose, device->deviceId, initError);

        return device->deviceId;
    }

    GenericDevice* DeviceManager::UnregisterDevice(size_t deviceId)
    {   
        //TODO: we need a list of accesses to this device, so we can notify them of pending disconnect.
        if (deviceId >= allDevices->Size())
        {
            Logf("Cannot unregister device %u: device id is too big!", LogSeverity::Error, deviceId);
            return nullptr;
        }

        if (allDevices->At(deviceId) == nullptr)
        {
            Logf("Cannot unregister device %u: device id not in use.", LogSeverity::Error, deviceId);
            return nullptr;
        }

        sl::SpinlockAcquire(&allDevicesLock);
        GenericDevice* dev = allDevices->At(deviceId);
        allDevices->At(deviceId) = nullptr;
        sl::SpinlockRelease(&allDevicesLock);

        if (GetPrimaryDevice(dev->type) == dev)
        {
            sl::ScopedSpinlock scopeLock(&primaryDevicesLock);

            //find next device of type, and make it the primary
            sl::Vector<size_t> nextDevices = FindDevicesOfType(dev->type);
            if (nextDevices.Empty())
                primaryDevices[(size_t)dev->type] = nullptr;
            else
                primaryDevices[(size_t)dev->type] = allDevices->At(nextDevices[0]);
        }

        if (dev->state == DeviceState::Ready)
        {
            dev->state = DeviceState::Deinitializing;
            dev->Deinit();
        }

        Logf("Device unregistered: id=%u, shutdownError=%b", LogSeverity::Verbose, dev->deviceId, dev->state == DeviceState::Shutdown);

        idAlloc->Free(dev->deviceId);
        dev->deviceId = 0;

        return dev;
    }

    sl::Opt<GenericDevice*> DeviceManager::GetDevice(size_t deviceId)
    {
        sl::ScopedSpinlock scopeLock(&allDevicesLock);
        
        for (size_t i = 0; i < allDevices->Size(); ++i)
        {
            if (allDevices->At(i) == nullptr)
                continue;
            if (i > deviceId)
                return {};

            if (allDevices->At(i)->deviceId == deviceId)
                return allDevices->At(i);
        }

        return {};
    }

    void DeviceManager::ResetDevice(size_t deviceId)
    {
        sl::Opt<GenericDevice*> maybeDevice = GetDevice(deviceId);
        if (!maybeDevice)
            return;

        GenericDevice* dev = *maybeDevice;
        dev->Reset();

        Logf("Device %u reset.", LogSeverity::Verbose, deviceId);
    }

    sl::Vector<size_t> DeviceManager::FindDevicesOfType(DeviceType type)
    {
        sl::ScopedSpinlock scopeLock(&allDevicesLock);
    
        sl::Vector<size_t> foundDevices;

        for (size_t i = 0; i < allDevices->Size(); i++)
        {
            if (allDevices->At(i) == nullptr)
                continue;
            
            if (allDevices->At(i)->type == type)
                foundDevices.PushBack(i);
        }

        return foundDevices;
    }

    sl::Opt<GenericDevice*> DeviceManager::GetPrimaryDevice(DeviceType type) const
    {   
        if (primaryDevices[(size_t)type] == nullptr)
            return {};
        
        return primaryDevices[(size_t)type];
    }

    void DeviceManager::SetPrimaryDevice(DeviceType type, size_t deviceId)
    {
        sl::Opt<GenericDevice*> maybeDevice = GetDevice(deviceId);
        if (!maybeDevice || *maybeDevice == nullptr)
            return;

        sl::ScopedSpinlock scopeLock(&primaryDevicesLock); //probably unnecessary as this will be an atomic write anyway.
        primaryDevices[(size_t)type] = *maybeDevice;
    }
}
