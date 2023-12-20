#include <drivers/DriverManager.h>
#include <drivers/DriverHelpers.h>
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
        "keyboard",
        "filesystem",
    };

    void PrintDriverInfo(DriverManifest& manifest)
    {
        if (!manifest.runtimeImage.Valid())
        {
            Log("|- %s (%s), not loaded.", LogLevel::Debug, manifest.friendlyName.C_Str(),
                manifest.sourcePath.C_Str());
            return;
        }

        Log("|- %s (%s), loadBase=0x%lx", LogLevel::Debug, manifest.friendlyName.C_Str(), 
            manifest.sourcePath.C_Str(), manifest.runtimeImage->loadBase);
        Log("|   ## procId=%lu, processEvent=%p", LogLevel::Debug, 
            manifest.process->Id(), manifest.ProcessEvent);

        auto symStore = manifest.runtimeImage->symbolRepo;
        Log("|   ## symbols: pub=%lu priv=%lu other=%lu", LogLevel::Debug, symStore->publicFunctions.Size(),
            symStore->privateFunctions.Size(), symStore->nonFunctions.Size());

        for (auto it = manifest.devices.Begin(); it != manifest.devices.End(); ++it)
        {
            auto& dev = *it;
            Log("|   -> (dev) %s", LogLevel::Debug, dev->friendlyName.C_Str());
        }
        for (auto it = manifest.apis.Begin(); it != manifest.apis.End(); ++it)
        {
            auto& api = *it;
            sl::StringSpan summaryStr = "<no summary>";
            if (api->api->get_summary != nullptr)
            {
                npk_string apiSummaryStr = api->api->get_summary(api->api);
                summaryStr = { apiSummaryStr.data, apiSummaryStr.length };
            }
            Log("|   <- (api) %s, %lu, %.*s", LogLevel::Debug, DeviceTypeStrs[(size_t)api->api->type], 
                api->api->id, (int)summaryStr.SizeBytes(), summaryStr.Begin());
        }
    }

    void DriverManager::PrintInfo()
    {
        manifestsLock.ReaderLock();
        for (auto it = manifests.Begin(); it != manifests.End(); ++it)
            PrintDriverInfo(***it);
        manifestsLock.ReaderUnlock();
    }

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
        mainThread->lock.Lock();
        mainThread->driverShadow = manifest;
        mainThread->lock.Unlock();
        
        using namespace Debug;
        auto eventHandlerSym = SymbolFromAddr(reinterpret_cast<uintptr_t>(manifest->ProcessEvent), 
            SymbolFlag::Public | SymbolFlag::Private);
        const char* handlerName = eventHandlerSym.HasValue() ? eventHandlerSym->name.Begin() : "<unknown>";

        Log("Loaded driver %s: mainThread=%lu, processEvent=%p (%s)", LogLevel::Info,
            manifest->friendlyName.C_Str(), mainThread->Id(), manifest->ProcessEvent, handlerName);
        mainThread->Start();
        stats.loadedCount++;
        return true;
    }

    bool DriverManager::AttachDevice(sl::Handle<DriverManifest>& driver, sl::Handle<DeviceDescriptor>& device)
    {
        VALIDATE_(driver.Valid(), false);
        VALIDATE_(device.Valid(), false);
        VALIDATE_(EnsureRunning(driver), false);

        SetShadow(driver);
        npk_event_new_device event;
        event.tags = device->initData;
        if (!driver->ProcessEvent(EventType::AddDevice, &event))
        {
            SetShadow(nullptr);
            Log("Failed to attach device %s to driver %s", LogLevel::Error, 
                device->friendlyName.C_Str(), driver->friendlyName.C_Str());
            return false;
        }

        SetShadow(nullptr);
        Log("Attached device %s to driver %s", LogLevel::Verbose, 
            device->friendlyName.C_Str(), driver->friendlyName.C_Str());
        device->attachedDriver = driver;
        stats.unclaimedDescriptors--;

        sl::ScopedLock scopeLock(driver->lock);
        driver->devices.PushBack(device);
        return true;
    }

    bool DriverManager::DetachDevice(sl::Handle<DeviceDescriptor>& device)
    {
        ASSERT_UNREACHABLE();
    }

    void DriverManager::SetShadow(sl::Handle<DriverManifest> shadow) const
    {
        auto& thread = Tasking::Thread::Current();
        sl::ScopedLock scopeLock(thread.lock);
        thread.driverShadow = shadow;
    }

    DriverManager globalDriverManager;
    DriverManager& DriverManager::Global()
    { return globalDriverManager; }

    void DriverManager::Init()
    {
        stats = {};
        apiIdAlloc = 1; //id 0 is reserved for 'null'.

        Log("Driver manager initialized, api version %u.%u.%u", LogLevel::Info,
            NP_MODULE_API_VER_MAJOR, NP_MODULE_API_VER_MINOR, NP_MODULE_API_VER_REV);
    }

    DriverStats DriverManager::GetStats() const
    {
        return stats;
    }

    sl::Handle<DriverManifest> DriverManager::GetShadow()
    {
        auto& thread = Tasking::Thread::Current();
        sl::ScopedLock scopeLock(thread.lock);
        return thread.driverShadow;
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

        stats.manifestCount++;
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
        stats.totalDescriptors++;
        stats.unclaimedDescriptors++;

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

    bool DriverManager::AddApi(npk_device_api* api, bool noOwner)
    {
        VALIDATE_(api != nullptr, false);

        sl::Handle<DriverManifest> shadow {};
        if (!noOwner)
        {
            shadow = GetShadow();
            VALIDATE_(shadow.Valid(), false);
        }

        if (!VerifyDeviceApi(api)) //check that the device api is implemented per-spec.
            return false;

        DeviceApi* device = new DeviceApi();
        device->api = api;
        device->references = 0;
        sl::NativePtr(&device->api->id).Write<size_t>(apiIdAlloc++);

        apiTreeLock.WriterLock();
        apiTree.Insert(device);
        apiTreeLock.WriterUnlock();
        if (noOwner)
        {
            sl::ScopedLock scopeLock(orphanLock);
            orphanApis.PushBack(device);
        }
        else
            shadow->apis.PushBack(device);

        sl::StringSpan summaryStr = "get_summary() not implemented.";
        if (api->get_summary != nullptr)
        {
            npk_string apiSummaryStr = api->get_summary(api);
            summaryStr = { apiSummaryStr.data, apiSummaryStr.length };
        }

        Log("Device API added %s: id=%lu, type=%s, %.*s", LogLevel::Info, noOwner ? "(unowned)" : "", 
            api->id, DeviceTypeStrs[api->type], (int)summaryStr.SizeBytes(), summaryStr.Begin());

        stats.apiCount++;
        return true;
    }

    bool DriverManager::RemoveApi(size_t id)
    {
        ASSERT_UNREACHABLE();
    }

    sl::Handle<DeviceApi> DriverManager::GetApi(size_t id)
    {
        apiTreeLock.ReaderLock();
        DeviceApi* scan = apiTree.GetRoot();
        while (scan != nullptr)
        {
            if (scan->api->id == id)
                break;
            if (scan->api->id > id)
                scan = apiTree.GetLeft(scan);
            else
                scan = apiTree.GetRight(scan);
        }
        apiTreeLock.ReaderUnlock();

        return scan;
    }
}
