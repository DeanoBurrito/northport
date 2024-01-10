#include <drivers/DriverManager.h>
#include <drivers/DriverHelpers.h>
#include <debug/Log.h>
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

        for (auto it = manifest.providedDevices.Begin(); it != manifest.providedDevices.End(); ++it)
        {
            auto dev = *it;
            Log("|   <- (dev %lu) %.*s", LogLevel::Debug, dev->id, 
                (int)dev->apiDesc->friendly_name.length, dev->apiDesc->friendly_name.data);
        }

        for (auto it = manifest.consumedDevices.Begin(); it != manifest.consumedDevices.End(); ++it)
        {
            auto dev = *it;
            Log("|   -> (dev %lu) %.*s", LogLevel::Debug, dev->id,
                (int)dev->apiDesc->friendly_name.length, dev->apiDesc->friendly_name.data);
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
        sl::Span<npk_load_name> loadNames(device->apiDesc->load_names, device->apiDesc->load_name_count);

        manifestsLock.ReaderLock();
        for (auto it = manifests.Begin(); it != manifests.End(); ++it)
        {
            auto scan = *it;
            for (size_t i = 0; i < loadNames.Size(); i++)
            {
                sl::Span<const uint8_t> loadName(loadNames[i].str, loadNames[i].length);
                if (static_cast<LoadType>(loadNames[i].type) != scan->loadType)
                    continue;
                if (loadName != scan->loadStr)
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

        Log("Loaded driver %s: mainThread=%lu, processEvent=%p", LogLevel::Info,
            manifest->friendlyName.C_Str(), mainThread->Id(), manifest->ProcessEvent);
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
        const auto* api = device->apiDesc;
        event.tags = api->init_data;
        if (!driver->ProcessEvent(EventType::AddDevice, &event))
        {
            SetShadow(nullptr);
            Log("Driver %s refused descriptor %lu (%.*s)", LogLevel::Error, driver->friendlyName.C_Str(),
                device->id, (int)api->friendly_name.length, api->friendly_name.data);
            return false;
        }

        SetShadow(nullptr);
        Log("Driver %s attached descriptor %lu (%.*s)", LogLevel::Verbose, driver->friendlyName.C_Str(),
                device->id, (int)api->friendly_name.length, api->friendly_name.data);
        device->attachedDriver = driver;
        stats.unclaimedDescriptors--;

        sl::ScopedLock scopeLock(driver->lock);
        driver->consumedDevices.PushBack(device);
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
        idAlloc = 1; //id 0 is reserved for 'null'.

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
            for (auto it = unclaimedDevices.Begin(); it != unclaimedDevices.End(); ++it)
            {
                auto& device = *it;
                sl::Span<npk_load_name> loadNames(device->apiDesc->load_names, device->apiDesc->load_name_count);

                for (size_t i = 0; i < loadNames.Size(); i++)
                {
                    sl::Span<const uint8_t> loadName(loadNames[i].str, loadNames[i].length);
                    if (static_cast<LoadType>(loadNames[i].type) != manifest->loadType)
                        continue;
                    if (loadName != manifest->loadStr)
                        continue;

                    if (AttachDevice(manifest, device))
                    {
                        unclaimedDevices.Erase(it);
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

    size_t DriverManager::AddDescriptor(npk_device_desc* descriptor)
    {
        VALIDATE_(descriptor != nullptr, NPK_INVALID_HANDLE);

        sl::Handle<DeviceDescriptor> desc = new DeviceDescriptor();
        desc->apiDesc = descriptor;
        desc->sourceDriver = GetShadow();
        desc->attachedDriver = nullptr;
        desc->id = idAlloc++;

        stats.totalDescriptors++;
        stats.unclaimedDescriptors++;

        sl::StringSpan srcName = "Kernel";
        if (desc->sourceDriver.Valid())
            srcName = desc->sourceDriver->friendlyName.Span();
        Log("%.*s added device desc (id=%lu) %.*s", LogLevel::Info, (int)srcName.Size(),
            srcName.Begin(), desc->id, (int)desc->apiDesc->friendly_name.length,
            desc->apiDesc->friendly_name.data);

        //try find a driver for our descriptor and load it.
        auto foundDriver = LocateDriver(desc);
        if (foundDriver.Valid() && AttachDevice(foundDriver, desc))
            return desc->id;

        //no driver found, add it to the unclaimed list.
        devicesLock.WriterLock();
        unclaimedDevices.PushBack(desc);
        devicesLock.WriterUnlock();

        return desc->id;
    }

    sl::Opt<void*> DriverManager::RemoveDescriptor(size_t descriptorId)
    {
        ASSERT_UNREACHABLE();
    }

    bool DriverManager::AddApi(npk_device_api* api, size_t parentApi, sl::Handle<DriverManifest> owner)
    {
        VALIDATE_(api != nullptr, false);
        VALIDATE(VerifyDeviceApi(api), false, "Malformed device API struct (unassigned function pointers?)");

        auto parentHandle = GetApi(parentApi);
        if (parentApi != NPK_INVALID_HANDLE)
            VALIDATE_(parentHandle.Valid(), false);

        DeviceApi* device = new DeviceApi();
        device->api = api;
        device->references = 0;
        if (parentHandle.Valid())
            device->parent = parentHandle;
        sl::NativePtr(&device->api->id).Write<size_t>(idAlloc++);

        apiTreeLock.WriterLock();
        apiTree.Insert(device);
        apiTreeLock.WriterUnlock();

        if (owner.Valid())
            owner->apis.PushBack(device);
        else
        {
            sl::ScopedLock scopeLock(orphanLock);
            orphanApis.PushBack(device);
        }

        npk_string summary {};
        if (api->get_summary != nullptr)
            summary = api->get_summary(api);

        sl::StringSpan ownerStr = owner.Valid() ? owner->friendlyName.Span() : "Kernel";
        Log("%.*s added device API, id=%lu, type=%s, summary: %.*s", LogLevel::Info, (int)ownerStr.Size(),
            ownerStr.Begin(), api->id, DeviceTypeStrs[api->type], (int)summary.length, summary.data);

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
