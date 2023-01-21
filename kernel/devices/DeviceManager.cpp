#include <devices/DeviceManager.h>
#include <debug/Log.h>

namespace Npk::Devices
{
    constexpr const char* DevTypeStrs[] = 
    {
        "Keyboard", "Pointer", "Framebuffer", "Serial"
    };
    
    DeviceManager globalDeviceManager;
    DeviceManager& DeviceManager::Global()
    { return globalDeviceManager; }

    void DeviceManager::Init()
    {
        nextId.Store(1);
        Log("Device manager initialized.", LogLevel::Info);
    }

    sl::Opt<size_t> DeviceManager::AttachDevice(GenericDevice* instance, bool delayInit)
    {
        if (instance == nullptr)
            return {};
        
        instance->id = nextId++; //fetch-add

        VALIDATE(instance->status == DeviceStatus::Offline, {}, "Device must be offline.")
        if (!delayInit)
        {
            VALIDATE(instance->Init(), {}, "Init() failed.");
            VALIDATE(instance->status == DeviceStatus::Online, {}, "Device not online.");
        }

        sl::ScopedLock scopeLock(listLock);
        devices.PushBack(instance);

        Log("Device attached: %lu, type=%lu (%s)", LogLevel::Verbose, instance->id, 
            (size_t)instance->Type(), DevTypeStrs[(size_t)instance->Type()]);
        return instance->id;
    }

    sl::Opt<GenericDevice*> DeviceManager::DetachDevice(size_t id, bool force)
    {
        sl::ScopedLock scopeLock(listLock);
        for (auto it = devices.Begin(); it != devices.End(); ++it)
        {
            if ((**it).id != id)
                continue;
            
            GenericDevice* dev = *it;
            ASSERT(dev != nullptr, "Device is nullptr.");

            if (dev->status == DeviceStatus::Offline)
            {
                Log("Detach offline device: id=%lu, type=%lu (%s)", LogLevel::Warning, id, 
                    (size_t)dev->type, DevTypeStrs[(size_t)dev->type]);
            }
            else if (!force && !dev->Deinit())
                return {};
            return devices.Erase(it); //TODO: retired ids pool
        }

        return {};
    }

    sl::Opt<GenericDevice*> DeviceManager::GetDevice(size_t id)
    {
        sl::ScopedLock scopeLock(listLock);
        for (auto it = devices.Begin(); it != devices.End(); ++it)
        {
            if ((**it).id == id)
                return *it;
        }

        return {};
    }

    sl::Vector<GenericDevice*> DeviceManager::GetDevices(DeviceType type)
    {
        sl::Vector<GenericDevice*> found;

        sl::ScopedLock scopeLock(listLock);
        for (auto it = devices.Begin(); it != devices.End(); ++it)
        {
            if ((**it).type != type)
                continue;
            found.PushBack(*it);
        }

        return found;
    }
}
