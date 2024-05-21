#include <drivers/DriverManager.h>
#include <drivers/DriverHelpers.h>
#include <debug/Log.h>
#include <memory/Vmm.h>
#include <tasking/Threads.h>

namespace Npk::Drivers
{
    constexpr const char* DeviceTypeStrs[] =
    { 
        "io",
        "framebuffer",
        "gpu",
        "keyboard",
        "filesystem",
        "syspower",
        "network",
    };

    static_assert(npk_device_api_type::Io == 0);
    static_assert(npk_device_api_type::Framebuffer == 1);
    static_assert(npk_device_api_type::Gpu == 2);
    static_assert(npk_device_api_type::Keyboard == 3);
    static_assert(npk_device_api_type::Filesystem == 4);
    static_assert(npk_device_api_type::SysPower == 5);

    constexpr size_t DeviceNodeStackReserveSize = 4;

    static void PrintNode(DeviceNode* node, size_t indent)
    {
        constexpr const char* TypeNames[] = { "driver", "descriptor", "api" };
        Log("%*c |- id=%lu type=%s", LogLevel::Debug, (int)indent * 2, ' ', 
            node->id, TypeNames[(size_t)node->type]);

        switch (node->type)
        {
        case DeviceNodeType::DriverInstance:
            {
                auto* driver = static_cast<DriverInstance*>(node);
                if (driver->manifest.Valid())
                {
                    Log("%*c |  name=%s, loadBase=0x%lx, transport=%lu", LogLevel::Debug, (int)indent * 2, ' ',
                        driver->manifest->friendlyName.C_Str(), driver->manifest->runtimeImage->loadBase,
                        driver->transportDevice.Valid() ? driver->transportDevice->id : 0);
                }
                else
                {
                    Log("%*c |  name=kernel, loadBase=0x%lx", LogLevel::Debug, (int)indent * 2, ' ',
                        -2 * GiB);
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
                Log("%*c |  name=%.*s", LogLevel::Debug, (int)indent * 2, ' ', 
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
                Log("%*c |  type=%s, summary=%.*s", LogLevel::Debug, (int)indent * 2, ' ',
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

    sl::Handle<DriverManifest> DriverManager::LocateDriver(sl::Span<npk_load_name> names, bool noLock)
    {
        //TODO: employ a hash table here or anything more efficient
        if (!noLock)
            manifestsLock.ReaderLock();
        for (auto it = manifests.Begin(); it != manifests.End(); ++it)
        {
            auto manifest = *it;
            for (size_t i = 0; i < manifest->loadNames.Size(); i++)
            {
                auto manifestName = manifest->loadNames[i];
                for (size_t j = 0; j < names.Size(); j++)
                {
                    auto testName = names[j];
                    if (testName.type != manifestName.type)
                        continue;
                    if (testName.length != manifestName.length)
                        continue;
                    if (sl::memcmp(testName.str, manifestName.str, testName.length) != 0)
                        continue;

                    //we found the one, only took 3 nested for loops
                    if (!noLock)
                        manifestsLock.ReaderUnlock();
                    return manifest;
                }
            }
        }

        if (!noLock)
            manifestsLock.ReaderUnlock();
        return {};
    }

    sl::Handle<DeviceDescriptor> DriverManager::LocateDevice(sl::Span<npk_load_name> names)
    {
        sl::ScopedLock scopeLock(unclaimedDevsLock);

        for (auto it = unclaimedDevs.Begin(); it != unclaimedDevs.End(); ++it)
        {
            auto dev = *it;
            for (size_t i = 0; i < dev->apiDesc->load_name_count; i++)
            {
                auto devName = dev->apiDesc->load_names[i];
                for (size_t j = 0; j < names.Size(); j++)
                {
                    auto testName = names[j];
                    if (testName.type != devName.type)
                        continue;
                    if (testName.length != devName.length)
                        continue;
                    if (sl::memcmp(testName.str, devName.str, testName.length) != 0)
                        continue;

                    return dev;
                }
            }
        }

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
        using namespace Tasking;

        auto prevShadow = Thread::Current().GetAttrib(ProgramAttribType::DriverShadow);
        if (shadow.Valid())
            shadow->references++; //the handle abstraction breaks down when it comes to program attribs
        sl::Span newShadow(reinterpret_cast<uint8_t*>(*shadow), 0);
        Thread::Current().SetAttrib(ProgramAttribType::DriverShadow, newShadow);

        if (prevShadow.Begin() != nullptr)
            reinterpret_cast<DriverInstance*>(prevShadow.Begin())->references--;
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
        using namespace Tasking;
        auto shadow = Thread::Current().GetAttrib(ProgramAttribType::DriverShadow);
        return reinterpret_cast<DriverInstance*>(shadow.Begin());
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
            
            auto conflictingDriver = LocateDriver({ manifest->loadNames.Begin(), manifest->loadNames.Size() }, true);
            if (conflictingDriver.Valid())
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

        //TODO: driver flags (always load)

        //check existing devices for any that might be handled by this driver.
        while (true)
        {
            auto device = LocateDevice({ manifest->loadNames.Begin(), manifest->loadNames.Size() });
            if (!device.Valid())
                break;

            if (!AttachDevice(manifest, device))
            {
                sl::ScopedLock scopeLock(unclaimedDevsLock);
                unclaimedDevs.PushBack(device);
            }
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
        auto foundDriver = LocateDriver({ desc->apiDesc->load_names, desc->apiDesc->load_name_count });
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
