#include <devices/DeviceManager.h>
#include <Locks.h>
#include <Log.h>
#include <devices/Keyboard.h>
#include <devices/Mouse.h>
#include <scheduling/ThreadGroup.h>
#include <devices/BootFramebuffer.h>

namespace Kernel::Devices
{
    DeviceManager deviceManagerGlobal;
    DeviceManager* DeviceManager::Global()
    { return &deviceManagerGlobal; }

    void DeviceManager::Init()
    {
        if (initialized)
            return;
        initialized = true;
        
        idAlloc = new sl::UIdAllocator();
        allDevices = new sl::Vector<GenericDevice*>();

        for (size_t i = 0; i < (size_t)DeviceType::EnumCount; ++i)
        {
            primaryDevices[i] = nullptr;
            aggregateIds[i] = (size_t)-1;
        }
        sl::SpinlockRelease(&allDevicesLock);
        sl::SpinlockRelease(&primaryDevicesLock);
        
        Log("Kernel device manager initialized.", LogSeverity::Info);

        RegisterBootFramebuffers(); //keep the bootloader-provided framebuffers, incase we dont find anything else we support later on.
        aggregateIds[(size_t)DeviceType::Keyboard] = RegisterDevice(Keyboard::Global());
        aggregateIds[(size_t)DeviceType::Mouse] = RegisterDevice(Mouse::Global());
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
        const bool initError = device->state != DeviceState::Ready;
        Logf("New device registered: id=%u, initError=%b, type=%u", LogSeverity::Verbose, device->deviceId, initError, device->type);

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

        Logf("Device unregistered: id=%u, shutdownError=%b", LogSeverity::Verbose, dev->deviceId, dev->state != DeviceState::Shutdown);

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

    size_t DeviceManager::GetAggregateId(DeviceType type) const
    { return aggregateIds[(size_t)type]; }

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

        if (type == DeviceType::GraphicsFramebuffer)
        {
            const Interfaces::GenericFramebuffer* fb = static_cast<const Interfaces::GenericFramebuffer*>(*maybeDevice);
            const Interfaces::FramebufferModeset mode = fb->GetCurrentMode();
            LogFramebuffer logfb;

            logfb.base.ptr = EnsureHigherHalfAddr(fb->GetAddress().Value().ptr);
            logfb.bpp = mode.bitsPerPixel;
            logfb.width = mode.width;
            logfb.height = mode.height;
            logfb.stride = mode.width * logfb.bpp / 8;
            logfb.pixelMask = 0xFFFF'FFFF;
            logfb.isNotBgr = false;
            if (mode.pixelFormat.blueOffset != 0 || mode.pixelFormat.redMask != 24)
                logfb.isNotBgr = true;
            SetLogFramebuffer(logfb);
        }
    }

    void DeviceManager::SubscribeToDeviceEvents(size_t deviceId)
    {
        auto maybeDevice = GetDevice(deviceId);
        if (!maybeDevice)
            return;

        if (maybeDevice.Value()->eventSubscribers == nullptr)
            return;

        sl::ScopedSpinlock scopeLock(&maybeDevice.Value()->lock);
        maybeDevice.Value()->eventSubscribers->PushBack(Scheduling::ThreadGroup::Current()->Id());
    }

    void DeviceManager::UnsubscribeFromDeviceEvents(size_t deviceId)
    {
        auto maybeDevice = GetDevice(deviceId);
        if (!maybeDevice)
            return;

        if (maybeDevice.Value()->eventSubscribers == nullptr)
            return;
        
        const size_t tgid = Scheduling::ThreadGroup::Current()->Id();
        sl::ScopedSpinlock scopeLock(&maybeDevice.Value()->lock);
        for (size_t i = 0; i < maybeDevice.Value()->eventSubscribers->Size(); i++)
        {
            if (maybeDevice.Value()->eventSubscribers->At(i) == tgid)
            {
                maybeDevice.Value()->eventSubscribers->Erase(i);
                return;
            }
        }
    }

    void DeviceManager::EventPump()
    {
        if (!initialized)
            return;
        
        sl::ScopedSpinlock scopeLock(&allDevicesLock);

        for (size_t i = 0; i < allDevices->Size(); i++)
        {
            if (allDevices->At(i) == nullptr)
                continue;
            if (allDevices->At(i)->eventSubscribers == nullptr)
                continue;

            allDevices->At(i)->EventPump();
        }
    }
}
