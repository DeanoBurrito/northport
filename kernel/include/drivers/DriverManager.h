#pragma once

#include <drivers/DriverManifest.h>
#include <drivers/InitTags.h>
#include <containers/LinkedList.h>
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
    
    struct DriverInstance
    {
        DriverManifest& manifest;
        DriverStatus status;
        void* instance;
    };
    
    class DriverManager
    {
    private:
        sl::LinkedList<DriverManifest> manifests;
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
        //TODO: UnloadDriver()
    };
}
