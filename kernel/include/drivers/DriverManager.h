#pragma once

#include <String.h>
#include <Handle.h>
#include <Locks.h>
#include <containers/Vector.h>
#include <containers/LinkedList.h>
#include <drivers/ElfLoader.h>
#include <drivers/api/Api.h>
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
    using ProcessPacketFunc = void (*)();

    struct DeviceDescriptor;

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
        Tasking::Process* process;

        ProcessEventFunc ProcessEvent;
        ProcessPacketFunc ProcessPacket;
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

    class DriverManager
    {
    private:
        sl::RwLock manifestsLock;
        sl::RwLock devicesLock;
        sl::LinkedList<sl::Handle<DriverManifest>> manifests;
        sl::LinkedList<sl::Handle<DeviceDescriptor>> devices;

        sl::Handle<DriverManifest> LocateDriver(sl::Handle<DeviceDescriptor>& device);
        bool EnsureRunning(sl::Handle<DriverManifest>& manifest);
        bool AttachDevice(sl::Handle<DriverManifest>& driver, sl::Handle<DeviceDescriptor>& device);
        bool DetachDevice(sl::Handle<DeviceDescriptor>& device);

    public:
        static DriverManager& Global();

        void Init();

        bool Register(sl::Handle<DriverManifest> manifest);
        bool Unregister(sl::StringSpan friendlyName);
        bool AddDevice(sl::Handle<DeviceDescriptor> device);
        bool RemoveDevice(sl::Handle<DeviceDescriptor> device);
    };
}
