#include <drivers/DriverManager.h>
#include <debug/Log.h>
#include <debug/Symbols.h>
#include <memory/Vmm.h>
#include <tasking/Scheduler.h>

namespace Npk::Drivers
{
    constexpr const char* DeviceTypeStrs[] =
    { 
        "io",
        "framebuffer",
        "gpu",
        "hid",
    };

    sl::Handle<DriverManifest> DriverManager::LocateDriver(sl::Handle<DeviceDescriptor>& device)
    {
        VALIDATE_(device.Valid(), {});

        manifestsLock.ReaderLock();
        for (auto it = manifests.Begin(); it != manifests.End(); ++it)
        {
            auto scan = *it;
            for (size_t i = 0; i < device->loadNames.Size(); i++)
            {
                if (device->loadNames[i].type != scan->loadType)
                    continue;
                if (device->loadNames[i].str != scan->loadStr)
                    continue;

                manifestsLock.ReaderUnlock();
                return scan;
            }
        }
        manifestsLock.ReaderUnlock();
        return {};
    }

    bool DriverManager::EnsureRunning(sl::Handle<DriverManifest>& manifest)
    {
        VALIDATE_(manifest.Valid(), false);

        sl::ScopedLock manifestLock(manifest->lock);
        if (manifest->runtimeImage.Valid())
            return true; //driver is already running

        //driver isnt running, load it and create a process + thread for it.
        LoadingDriverInfo loadInfo;
        loadInfo.manifest = nullptr;
        loadInfo.name = manifest->friendlyName.Span();
        manifest->runtimeImage = LoadElf(&VMM::Kernel(), manifest->sourcePath.Span(), &loadInfo);
        VALIDATE_(manifest->runtimeImage.Valid(), false);
        VALIDATE_(loadInfo.manifest != nullptr, false);

        auto apiManifest = static_cast<const npk_driver_manifest*>(loadInfo.manifest);
        manifest->ProcessEvent = reinterpret_cast<ProcessEventFunc>(apiManifest->process_event);

        using namespace Tasking;
        auto entryFunction = reinterpret_cast<ThreadMain>(manifest->runtimeImage->entryAddr);
        manifest->process = Scheduler::Global().CreateProcess();
        auto mainThread = Scheduler::Global().CreateThread(entryFunction, nullptr, manifest->process);
        mainThread->DriverShadow() = manifest;
        
        using namespace Debug;
        auto eventHandlerSym = SymbolFromAddr(reinterpret_cast<uintptr_t>(manifest->ProcessEvent), 
            SymbolFlag::Public | SymbolFlag::Private);
        const char* handlerName = eventHandlerSym.HasValue() ? eventHandlerSym->name.Begin() : "<unknown>";

        Log("Loaded driver %s: mainThread=%lu, processEvent=%p (%s)", LogLevel::Info,
            manifest->friendlyName.C_Str(), mainThread->Id(), manifest->ProcessEvent, handlerName);
        mainThread->Start();
        return true;
    }

    bool DriverManager::AttachDevice(sl::Handle<DriverManifest>& driver, sl::Handle<DeviceDescriptor>& device)
    {
        VALIDATE_(driver.Valid(), false);
        VALIDATE_(device.Valid(), false);
        VALIDATE_(EnsureRunning(driver), false);

        Tasking::Thread::Current().DriverShadow() = driver;
        npk_event_new_device event;
        event.tags = device->initData;
        if (!driver->ProcessEvent(EventType::AddDevice, &event))
        {
            Tasking::Thread::Current().DriverShadow() = nullptr;
            Log("Failed to attach device %s to driver %s", LogLevel::Error, 
                device->friendlyName.C_Str(), driver->friendlyName.C_Str());
            return false;
        }

        Tasking::Thread::Current().DriverShadow() = nullptr;
        Log("Attached device %s to driver %s", LogLevel::Verbose, 
            device->friendlyName.C_Str(), driver->friendlyName.C_Str());
        device->attachedDriver = driver;

        sl::ScopedLock scopeLock(driver->lock);
        driver->devices.PushBack(device);
        return true;
    }

    bool DriverManager::DetachDevice(sl::Handle<DeviceDescriptor>& device)
    {
        ASSERT_UNREACHABLE();
    }

    DriverManager globalDriverManager;
    DriverManager& DriverManager::Global()
    { return globalDriverManager; }

    void DriverManager::Init()
    {
        apiIdAlloc = 1; //id 0 is reserved for 'null'.

        Log("Driver manager initialized, api version %u.%u.%u", LogLevel::Info,
            NP_MODULE_API_VER_MAJOR, NP_MODULE_API_VER_MINOR, NP_MODULE_API_VER_REV);
    }

    sl::Handle<DriverManifest> DriverManager::GetShadow()
    {
        return Tasking::Thread::Current().DriverShadow();
    }

    bool DriverManager::AddManifest(sl::Handle<DriverManifest> manifest)
    {
        manifestsLock.WriterLock();
        //check that the manifest doesnt have a conflicting friendly name or load type/string.
        for (auto it = manifests.Begin(); it != manifests.End(); ++it)
        {
            sl::Handle<DriverManifest> scan = *it;
            if (scan->friendlyName == manifest->friendlyName)
            {
                manifestsLock.WriterUnlock();
                Log("Failed to register driver with duplicate name: %s", LogLevel::Error, 
                    manifest->friendlyName.C_Str());
                return false;
            }
            if (scan->loadType == manifest->loadType && scan->loadStr == manifest->loadStr)
            {
                manifestsLock.WriterUnlock();
                Log("Failed to register driver with duplicate load string: %s", LogLevel::Error,
                    manifest->friendlyName.C_Str());
                return false;
            }
        }

        manifests.PushBack(manifest);
        manifestsLock.WriterUnlock();

        Log("Driver manifest added: %s, module=%s", LogLevel::Info, manifest->friendlyName.C_Str(), 
            manifest->sourcePath.C_Str());

        //some drivers might be purely software-driven and want to always be loaded, handle them.
        if (manifest->loadType == LoadType::Always)
        {
            Log("Loading driver %s due to loadtype=always", LogLevel::Info, manifest->friendlyName.C_Str());
            if (!EnsureRunning(manifest))
                Log("Failed to load always-load driver %s", LogLevel::Error, manifest->friendlyName.C_Str());
        }

        //check existing devices for any that might be handled by this driver.
        devicesLock.WriterLock();
        while (true)
        {
            bool finishedEarly = false;
            for (auto it = devices.Begin(); it != devices.End(); ++it)
            {
                auto& device = *it;
                for (size_t i = 0; i < device->loadNames.Size(); i++)
                {
                    if (device->loadNames[i].type != manifest->loadType)
                        continue;
                    if (device->loadNames[i].str != manifest->loadStr)
                        continue;

                    if (AttachDevice(manifest, device))
                    {
                        devices.Erase(it);
                        finishedEarly = true;
                        break;
                    }
                }
            }
            if (!finishedEarly)
                break;
        }
        devicesLock.WriterUnlock();

        return true;
    }

    bool DriverManager::RemoveManifest(sl::StringSpan friendlyName)
    {
        ASSERT_UNREACHABLE();
    }

    bool DriverManager::AddDescriptor(sl::Handle<DeviceDescriptor> device)
    {
        //before add descriptor to list of unclaimed devices, see if we can already attach a driver.
        auto foundDriver = LocateDriver(device);
        if (foundDriver.Valid() && AttachDevice(foundDriver, device))
            return true;

        //no driver currently available, stash descriptor for later (or never).
        devicesLock.WriterLock();
        devices.PushBack(device);
        devicesLock.WriterUnlock();
        Log("New unclaimed device: %s", LogLevel::Verbose, device->friendlyName.C_Str());
        return true;
    }

    bool DriverManager::RemoveDescriptor(sl::Handle<DeviceDescriptor> device)
    {
        ASSERT_UNREACHABLE();
    }

    bool DriverManager::AddApi(npk_device_api* api)
    {
        VALIDATE_(api != nullptr, false);

        auto shadow = GetShadow();
        VALIDATE(shadow.Valid(), false, "Device APIs cannot be added outside of a driver shadow.");

        DeviceApi* device = new DeviceApi();
        device->api = api;
        device->references = 0;
        sl::NativePtr(&device->api->id).Write(apiIdAlloc++);

        apiTreeLock.WriterLock();
        apiTree.Insert(device);
        apiTreeLock.WriterUnlock();

        Log("Device API added: id=%lu, type=%s", LogLevel::Info, device->api->id, 
            DeviceTypeStrs[device->api->type]);
        return true;
    }

    bool DriverManager::RemoveApi(size_t id)
    {
        ASSERT_UNREACHABLE();
    }
}
