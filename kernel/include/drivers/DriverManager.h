#pragma once

#include <containers/LinkedList.h>
#include <containers/Vector.h>
#include <drivers/DriverManifest.h>
#include <Optional.h>

namespace Kernel::Drivers
{    
    struct DriverExtendedManifest
    {
        DriverManifest manifest;
        sl::Vector<GenericDriver*> instances; //instance id is an index into this

        DriverExtendedManifest(const DriverManifest& manifest) : manifest(manifest)
        {}
    };
    
    class DriverManager
    {
    private:
        char lock;
        sl::LinkedList<DriverExtendedManifest>* manifests;

        void RegisterBuiltIns();
        DriverExtendedManifest* GetExtendedManifest(const DriverSubsystem subsystem, const DriverMachineName& machineName);

    public:
        static DriverManager* Global();

        void Init();
        void RegisterDriver(const DriverManifest& manifest);

        sl::Opt<DriverManifest*> FindDriver(DriverSubsystem subsystem, DriverMachineName machineName);
        bool StartDriver(const DriverManifest* manifest, DriverInitTag* userTags);
        bool StopDriver(const DriverManifest* manifest, size_t instanceNumber);
        void InjectEvent(const DriverManifest* manifest, size_t instance, DriverEventType type, void* arg);

        GenericDriver* GetDriverInstance(const DriverManifest* manifest, size_t instanceNumber);
    };
}
