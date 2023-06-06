#pragma once

#include <drivers/DriverManifest.h>
#include <drivers/InitTags.h>
#include <tasking/Process.h>
#include <containers/LinkedList.h>
#include <Locks.h>
#include <Span.h>

namespace Npk::Drivers
{
    struct LoadedDriver
    {
        Tasking::Process* process;
        DriverManifest& manifest;

        LoadedDriver(DriverManifest& man) : manifest(man)
        {}
    };
    
    class DriverManager
    {
    private:
        sl::LinkedList<DriverManifest> manifests;
        sl::TicketLock lock;

        DriverManifest* FindByFriendlyName(sl::StringSpan name);
        DriverManifest* FindByMachName(ManifestName name);

        bool KillDriver(LoadedDriver* driver);

    public:
        static DriverManager& Global();

        DriverManager() = default;
        DriverManager(const DriverManager&) = delete;
        DriverManager& operator=(const DriverManager&) = delete;
        DriverManager(DriverManager&&) = delete;
        DriverManager& operator=(DriverManager&&) = delete;

        void Init();

        void RegisterDriver(const DriverManifest& manifest);
        bool TryLoadDriver(ManifestName machineName, InitTag* tags);
        bool UnloadDriver(ManifestName machineName);
        bool UnloadDriver(sl::StringSpan friendlyName);
    };
}
