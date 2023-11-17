#pragma once

#include <String.h>
#include <Handle.h>
#include <Locks.h>
#include <containers/Vector.h>
#include <containers/LinkedList.h>
#include <drivers/ElfLoader.h>
#include <drivers/api/Api.h>
#include <drivers/api/Drivers.h>
#include <tasking/Process.h>

namespace Npk::Drivers
{
    enum class LoadType
    {
        Never = 0,
        Always = 1,
        PciClass = 2,
        PciId = 3,
        DtbCompat = 4,
    };

    enum class EventType
    {
        Exit = 0,
        AddDevice = 1,
        RemoveDevice = 2,
    };

    using ProcessEventFunc = bool (*)(EventType type, void* arg);

    struct DeviceDescriptor;
    struct DeviceApi;

    struct DriverManifest
    {
        sl::Atomic<size_t> references;
        sl::SpinLock lock;
        sl::String sourcePath;
        sl::String friendlyName;
        LoadType loadType;
        sl::Span<const uint8_t> loadStr;

        sl::Handle<LoadedElf> runtimeImage;
        sl::LinkedList<sl::Handle<DeviceDescriptor>> devices;
        sl::LinkedList<npk_device_api*> apis;
        Tasking::Process* process;

        ProcessEventFunc ProcessEvent;
    };

    struct DeviceLoadName
    {
        LoadType type;
        sl::Span<const uint8_t> str;
    };

    struct DeviceDescriptor
    {
        sl::Atomic<size_t> references;
        sl::String friendlyName;
        sl::Vector<DeviceLoadName> loadNames;
        npk_init_tag* initData;

        sl::Handle<DriverManifest> attachedDriver;
    };

    struct DeviceApi
    {
        sl::Atomic<size_t> references;
        sl::RBTreeHook hook;

        npk_device_api* api;
    };
    
    struct DeviceApiLess
    {
        bool operator()(const DeviceApi& a, const DeviceApi& b)
        { return a.api->id < b.api->id; }
    };

    using DeviceApiTree = sl::RBTree<DeviceApi, &DeviceApi::hook, DeviceApiLess>;

    class DriverManager
    {
    private:
        sl::RwLock manifestsLock;
        sl::RwLock devicesLock;
        sl::RwLock apiTreeLock;
        sl::LinkedList<sl::Handle<DriverManifest>> manifests;
        sl::LinkedList<sl::Handle<DeviceDescriptor>> devices;
        DeviceApiTree apiTree;

        sl::Atomic<size_t> apiIdAlloc;

        sl::Handle<DriverManifest> LocateDriver(sl::Handle<DeviceDescriptor>& device);
        bool EnsureRunning(sl::Handle<DriverManifest>& manifest);
        bool AttachDevice(sl::Handle<DriverManifest>& driver, sl::Handle<DeviceDescriptor>& device);
        bool DetachDevice(sl::Handle<DeviceDescriptor>& device);

    public:
        static DriverManager& Global();

        void Init();
        sl::Handle<DriverManifest> GetShadow();

        bool AddManifest(sl::Handle<DriverManifest> manifest);
        bool RemoveManifest(sl::StringSpan friendlyName);

        bool AddDescriptor(sl::Handle<DeviceDescriptor> device);
        bool RemoveDescriptor(sl::Handle<DeviceDescriptor> device);

        bool AddApi(npk_device_api* api);
        bool RemoveApi(size_t id);
    };
}
