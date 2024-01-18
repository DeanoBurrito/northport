#pragma once

#include <drivers/ElfLoader.h>
#include <interfaces/driver/Api.h>
#include <interfaces/driver/Drivers.h>
#include <interfaces/driver/Devices.h>
#include <String.h>
#include <Handle.h>
#include <Locks.h>
#include <containers/Vector.h>
#include <containers/LinkedList.h>
#include <containers/RBTree.h>

namespace Npk::Drivers
{
    enum class DeviceNodeType
    {
        DriverInstance,
        Descriptor,
        Api,
    };

    struct DeviceNode
    {
        sl::RBTreeHook hook;
        DeviceNodeType type;
        sl::Atomic<size_t> references;
        size_t id;
    };

    struct DeviceNodeLess
    {
        bool operator()(const DeviceNode& a, const DeviceNode& b)
        { return a.id < b.id; }
    };

    enum class LoadType
    {
        Never = 0,
        Always = 1,
        PciClass = 2,
        PciId = 3,
        PciHost = 4,
        DtbCompat = 5,
    };

    enum class EventType
    {
        Init = 0,
        Exit = 1,
        AddDevice = 2,
        RemoveDevice = 3,
    };

    using ProcessEventFunc = bool (*)(EventType type, void* arg);

    struct DeviceDescriptor;
    struct DriverInstance;

    struct DeviceApi : public DeviceNode
    {
        sl::Handle<DriverInstance> driver;
        npk_device_api* api;
    };

    struct DriverManifest
    {
        sl::Atomic<size_t> references;
        sl::SpinLock lock;
        sl::String sourcePath;
        sl::String friendlyName;
        LoadType loadType;
        sl::Span<const uint8_t> loadStr;

        sl::Handle<LoadedElf> runtimeImage;
        ProcessEventFunc ProcessEvent;
    };

    struct DriverInstance : public DeviceNode
    {
        sl::Handle<DriverManifest> manifest;

        sl::SpinLock lock;
        sl::Handle<DeviceDescriptor> consumedDevice;
        sl::Handle<DeviceApi> transportDevice;
        sl::LinkedList<sl::Handle<DeviceDescriptor>> providedDevices;
        sl::LinkedList<sl::Handle<DeviceApi>> apis;
    };

    struct DeviceLoadName
    {
        LoadType type;
        sl::Span<const uint8_t> str;
    };

    struct DeviceDescriptor : public DeviceNode
    {
        npk_device_desc* apiDesc;
        sl::Handle<DriverInstance> sourceDriver;
        sl::Handle<DriverInstance> attachedDriver;
    };

    struct DriverStats
    {
        size_t manifestCount;
        size_t loadedCount;
        size_t unclaimedDescriptors;
        size_t totalDescriptors;
        size_t apiCount;
    };

    class DriverManager
    {
    private:
        sl::RwLock nodeTreeLock;
        sl::RBTree<DeviceNode, &DeviceNode::hook, DeviceNodeLess> nodeTree;
        sl::SpinLock unclaimedDevsLock;
        sl::LinkedList<sl::Handle<DeviceDescriptor>> unclaimedDevs;

        sl::RwLock manifestsLock;
        sl::LinkedList<sl::Handle<DriverManifest>> manifests;

        DriverInstance kernelInstance;
        DriverStats stats;
        sl::Atomic<size_t> idAlloc;

        sl::Handle<DriverManifest> LocateDriver(sl::Handle<DeviceDescriptor>& device);
        bool EnsureRunning(sl::Handle<DriverManifest>& manifest);
        bool AttachDevice(sl::Handle<DriverManifest>& driver, sl::Handle<DeviceDescriptor>& device);
        bool DetachDevice(sl::Handle<DeviceDescriptor>& device);

        void SetShadow(sl::Handle<DriverInstance> shadow) const;
        void AddNode(sl::Handle<DeviceNode> node);

    public:
        static DriverManager& Global();

        void Init();
        void PrintInfo();
        DriverStats GetStats() const;
        sl::Handle<DriverInstance> GetShadow();
        bool SetTransportApi(sl::Handle<DriverInstance> driver, size_t api);

        bool AddManifest(sl::Handle<DriverManifest> manifest);
        bool RemoveManifest(sl::StringSpan friendlyName);
        size_t AddDescriptor(npk_device_desc* descriptor);
        sl::Opt<void*> RemoveDescriptor(size_t descriptorId);
        bool AddApi(npk_device_api* api, sl::Handle<DriverInstance> owner);
        bool RemoveApi(size_t id);

        sl::Handle<DeviceNode> GetById(size_t id);
        sl::Vector<sl::Handle<DeviceNode>> GetStackFromId(size_t id);
        sl::Handle<DriverInstance> GetInstance(size_t id);
        sl::Handle<DeviceDescriptor> GetDescriptor(size_t id);
        sl::Handle<DeviceApi> GetApi(size_t id);
    };
}
