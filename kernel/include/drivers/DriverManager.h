#pragma once

#include <drivers/DriverManifest.h>
#include <drivers/InitTags.h>
#include <tasking/Process.h>
#include <containers/LinkedList.h>
#include <containers/Vector.h>
#include <Optional.h>
#include <Locks.h>

namespace Npk::Drivers
{
    enum class DriverStatus
    {
        PendingOnline = 0,
        Online,
        PendingOffline,
        Offline,
        Error,
    };
    
    struct LoadedDriver
    {
        DriverManifest& manifest;
        Tasking::Process* proc;
    };
    
    class DriverManager
    {
    private:
        sl::LinkedList<DriverManifest> manifests;
        sl::LinkedList<LoadedDriver> instances;
        sl::TicketLock lock;

    public:
        static DriverManager& Global();

        DriverManager() = default;
        DriverManager(const DriverManager&) = delete;
        DriverManager& operator=(const DriverManager&) = delete;
        DriverManager(DriverManager&&) = delete;
        DriverManager& operator=(DriverManager&&) = delete;

        void Init();

        void RegisterDriver(const DriverManifest& manifest);
        bool TryLoadDriver(ManifestName name, InitTag* tags);
        bool UnloadDriver(ManifestName name, bool force = false);
        bool UnloadDriver(const char* friendlyName, bool force = false);
    };
}
