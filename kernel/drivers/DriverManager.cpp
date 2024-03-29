#include <drivers/DriverManager.h>
#include <drivers/DriverHelpers.h>
#include <debug/Log.h>
#include <memory/Vmm.h>
#include <tasking/Thread.h>

namespace Npk::Drivers
{
    //TOOD: binding assert checks!
    constexpr const char* DeviceTypeStrs[] =
    { 
        "io",
        "framebuffer",
        "gpu",
        "keyboard",
        "filesystem",
    };

    constexpr size_t DeviceNodeStackReserveSize = 4;

    static void PrintNode(DeviceNode* node, size_t indent)
    {
        constexpr const char* TypeNames[] = { "driver", "descriptor", "api" };
        Log("%*.0s |- id=%lu type=%s", LogLevel::Debug, (int)indent * 2, (char*)nullptr, 
            node->id, TypeNames[(size_t)node->type]);

        switch (node->type)
        {
        case DeviceNodeType::DriverInstance:
            {
                auto* driver = static_cast<DriverInstance*>(node);
                if (driver->manifest.Valid())
                {
                    Log("%*.0s |  name=%s, loadBase=0x%lx", LogLevel::Debug, (int)indent * 2, (char*)nullptr,
                        driver->manifest->friendlyName.C_Str(), driver->manifest->runtimeImage->loadBase);
                }

                for (auto it = driver->apis.Begin(); it != driver->apis.End(); ++it)
                    PrintNode(**it, indent + 1);
                for (auto it = driver->providedDevices.Begin(); it != driver->providedDevices.End(); ++it)
                    PrintNode(**it, indent + 1);
                break;
            }
        case DeviceNodeType::Descriptor:
            {
                auto* desc = static_cast<DeviceDescriptor*>(node);
                Log("%*.s |  name=%.*s", LogLevel::Debug, (int)indent * 2, (char*)nullptr, 
                    (int)desc->apiDesc->friendly_name.length, desc->apiDesc->friendly_name.data);
                if (desc->attachedDriver.Valid())
                    PrintNode(*desc->attachedDriver, indent + 1);
                break;
            }
        case DeviceNodeType::Api:
            {
                auto* api = static_cast<DeviceApi*>(node);
                npk_string summary {};
                if (api->api->get_summary != nullptr)
                    summary = api->api->get_summary(api->api);
                Log("%*s |  type=%s, summary=%.*s", LogLevel::Debug, (int)indent * 2, (char*)nullptr,
                    DeviceTypeStrs[api->api->type], (int)summary.length, summary.data);
                break;
            }
        }
    }

    void DriverManager::PrintInfo()
    {
        manifestsLock.ReaderLock();
        for (auto it = manifests.Begin(); it != manifests.End(); ++it)
        {
            auto manifest = *it;
            Log("Manifest: name=%s, source=%s, rc=%lu", LogLevel::Debug,
                manifest->friendlyName.C_Str(), manifest->sourcePath.C_Str(), manifest->references.Load());
            if (manifest->runtimeImage.Valid())
            {
                Log("  loadBase=0x%lx, procEvent=%p", LogLevel::Debug,
                    manifest->runtimeImage->loadBase, manifest->ProcessEvent);
            }
        }
        manifestsLock.ReaderUnlock();

        nodeTreeLock.ReaderLock();
        PrintNode(&kernelInstance, 0);
        nodeTreeLock.ReaderUnlock();
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

        Log("Loaded driver %s: processEvent=%p", LogLevel::Info, manifest->friendlyName.C_Str(),  manifest->ProcessEvent);
        stats.loadedCount++;

        ASSERT_(manifest->ProcessEvent(EventType::Init, nullptr));
        return true;
    }

    bool DriverManager::AttachDevice(sl::Handle<DriverManifest>& driver, sl::Handle<DeviceDescriptor>& device)
    {
        VALIDATE_(driver.Valid(), false);
        VALIDATE_(device.Valid(), false);
        VALIDATE_(EnsureRunning(driver), false);

        sl::Handle<DriverInstance> instance = new DriverInstance();
        instance->type = DeviceNodeType::DriverInstance;
        instance->id = idAlloc++;
        instance->manifest = driver;
        instance->consumedDevice = device;
        device->attachedDriver = instance;

        nodeTreeLock.WriterLock();
        nodeTree.Insert(*instance);
        nodeTreeLock.WriterUnlock();

        auto prevShadow = GetShadow();
        SetShadow(instance);
        npk_event_add_device event;
        const auto* api = device->apiDesc;
        event.tags = api->init_data;
        event.descriptor_id = device->id;

        if (!driver->ProcessEvent(EventType::AddDevice, &event))
        {
            SetShadow(prevShadow);
            Log("Driver %s refused descriptor %lu (%.*s)", LogLevel::Error, driver->friendlyName.C_Str(),
                device->id, (int)api->friendly_name.length, api->friendly_name.data);
            //TOOD: free instance, remove from tree
            return false;
        }

        SetShadow(prevShadow);
        Log("Driver %s attached descriptor %lu (%.*s)", LogLevel::Verbose, driver->friendlyName.C_Str(),
                device->id, (int)api->friendly_name.length, api->friendly_name.data);
        stats.unclaimedDescriptors--;

        return true;
    }

    bool DriverManager::DetachDevice(sl::Handle<DeviceDescriptor>& device)
    {
        ASSERT_UNREACHABLE();
    }

    void DriverManager::SetShadow(sl::Handle<DriverInstance> shadow) const
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

        kernelInstance.references = 1;
        kernelInstance.id = idAlloc++;
        nodeTree.Insert(&kernelInstance);

        Log("Driver manager initialized, api version %u.%u.%u", LogLevel::Info,
            NP_MODULE_API_VER_MAJOR, NP_MODULE_API_VER_MINOR, NP_MODULE_API_VER_REV);
    }

    DriverStats DriverManager::GetStats() const
    {
        return stats;
    }

    sl::Handle<DriverInstance> DriverManager::GetShadow()
    {
        auto& thread = Tasking::Thread::Current();
        sl::ScopedLock scopeLock(thread.lock);
        return thread.driverShadow;
    }

    bool DriverManager::SetTransportApi(sl::Handle<DriverInstance> driver, size_t api)
    {
        VALIDATE_(driver.Valid(), false);
        auto apiHandle = GetApi(api);
        VALIDATE_(apiHandle.Valid(), false);

        driver->transportDevice = apiHandle;
        return true;
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

        Log("Driver manifest added: %s, module=%s", LogLevel::Info, manifest->friendlyName.C_Str()
            , manifest->sourcePath.C_Str());

        //some drivers might be purely software-driven and want to always be loaded, handle them.
        if (manifest->loadType == LoadType::Always)
        {
            Log("Loading driver %s due to loadtype=always", LogLevel::Info, manifest->friendlyName.C_Str());
            if (!EnsureRunning(manifest))
                Log("Failed to load always-load driver %s", LogLevel::Error, manifest->friendlyName.C_Str());
        }

        //check existing devices for any that might be handled by this driver.
        sl::ScopedLock devsLock(unclaimedDevsLock);
        while (true)
        {
            bool finishedEarly = false;
            for (auto it = unclaimedDevs.Begin(); it != unclaimedDevs.End(); ++it)
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
                        unclaimedDevs.Erase(it);
                        finishedEarly = true;
                        break;
                    }
                }
            }
            if (!finishedEarly)
                break;
        }

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
        desc->type = DeviceNodeType::Descriptor;

        if (!desc->sourceDriver.Valid())
            desc->sourceDriver = &kernelInstance;

        desc->sourceDriver->lock.Lock();
        desc->sourceDriver->providedDevices.PushBack(desc);
        desc->sourceDriver->lock.Unlock();

        stats.totalDescriptors++;
        stats.unclaimedDescriptors++;

        sl::StringSpan srcName = "kernel";
        if (desc->sourceDriver->manifest.Valid())
            srcName = desc->sourceDriver->manifest->friendlyName.Span();
        Log("New descriptor from %.*s, id=%lu: %.*s", LogLevel::Info, (int)srcName.Size(),
            srcName.Begin(), desc->id, (int)desc->apiDesc->friendly_name.length,
            desc->apiDesc->friendly_name.data);

        nodeTreeLock.WriterLock();
        nodeTree.Insert(*desc);
        nodeTreeLock.WriterUnlock();

        //try find a driver for our descriptor and load it.
        auto foundDriver = LocateDriver(desc);
        if (foundDriver.Valid() && AttachDevice(foundDriver, desc))
            return desc->id;

        //no driver found, add it to the unclaimed list.
        sl::ScopedLock devsLock(unclaimedDevsLock);
        unclaimedDevs.PushBack(desc);
        return desc->id;
    }

    sl::Opt<void*> DriverManager::RemoveDescriptor(size_t descriptorId)
    {
        ASSERT_UNREACHABLE();
    }

    bool DriverManager::AddApi(npk_device_api* api, sl::Handle<DriverInstance> owner)
    {
        VALIDATE_(api != nullptr, false);
        VALIDATE(VerifyDeviceApi(api), false, "Malformed device API struct (unassigned function pointers?)");

        if (!owner.Valid())
            owner = &kernelInstance;

        DeviceApi* device = new DeviceApi();
        device->api = api;
        device->references = 0;
        device->type = DeviceNodeType::Api;
        device->driver = owner;
        sl::NativePtr(&device->api->id).Write<size_t>(idAlloc++);
        device->id = device->api->id;
        
        nodeTreeLock.WriterLock();
        nodeTree.Insert(device);
        nodeTreeLock.WriterUnlock();

        owner->apis.PushBack(device);

        npk_string summary {};
        if (api->get_summary != nullptr)
            summary = api->get_summary(api);

        sl::StringSpan ownerStr = owner->manifest.Valid() ? owner->manifest->friendlyName.Span() : "Kernel";
        Log("%.*s added device API, id=%lu, type=%s, summary: %.*s", LogLevel::Info, (int)ownerStr.Size(),
            ownerStr.Begin(), api->id, DeviceTypeStrs[api->type], (int)summary.length, summary.data);

        stats.apiCount++;
        return true;
    }

    bool DriverManager::RemoveApi(size_t id)
    {
        ASSERT_UNREACHABLE();
    }

    sl::Handle<DeviceNode> DriverManager::GetById(size_t id)
    {
        if (id == NPK_INVALID_HANDLE)
            return {};

        nodeTreeLock.WriterLock();
        sl::Handle<DeviceNode> scan = nodeTree.GetRoot();
        while (scan.Valid())
        {
            if (scan->id == id)
            {
                nodeTreeLock.WriterUnlock();
                return scan;
            }

            if (id < scan->id)
                scan = nodeTree.GetLeft(*scan);
            else
                scan = nodeTree.GetRight(*scan);
        }

        nodeTreeLock.WriterUnlock();
        return {};
    }

    sl::Vector<sl::Handle<DeviceNode>> DriverManager::GetStackFromId(size_t id)
    {
        sl::Vector<sl::Handle<DeviceNode>> stack(DeviceNodeStackReserveSize);

        auto node = GetById(id);
        while (node.Valid())
        {
            stack.PushBack(node);
            
            //TODO: cleaner way of doing this? would rather not introduce inheritence just for this lol
            switch (node->type)
            {
            case DeviceNodeType::DriverInstance:
                {
                    auto* instance = static_cast<DriverInstance*>(*node);
                    node = *instance->consumedDevice;
                    break;
                }
            case DeviceNodeType::Descriptor:
                {
                    auto* desc = static_cast<DeviceDescriptor*>(*node);
                    node = *desc->sourceDriver;
                    break;
                }
            case DeviceNodeType::Api:
                {
                    auto* api = static_cast<DeviceApi*>(*node);
                    node = *api->driver;
                    break;
                }
            }
        }

        return stack;
    }

    sl::Handle<DriverInstance> DriverManager::GetInstance(size_t id)
    {
        auto found = GetById(id);
        if (!found.Valid() || found->type != DeviceNodeType::DriverInstance)
            return {};
        return static_cast<DriverInstance*>(*found);
    }

    sl::Handle<DeviceDescriptor> DriverManager::GetDescriptor(size_t id)
    {
        auto found = GetById(id);
        if (!found.Valid() || found->type != DeviceNodeType::Descriptor)
            return {};
        return static_cast<DeviceDescriptor*>(*found);
    }

    sl::Handle<DeviceApi> DriverManager::GetApi(size_t id)
    {
        auto found = GetById(id);
        if (!found.Valid() || found->type != DeviceNodeType::Api)
            return {};
        return static_cast<DeviceApi*>(*found);
    }
}
