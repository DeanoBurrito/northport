#include <drivers/DriverManager.h>
#include <debug/Log.h>
#include <tasking/Thread.h>
#include <Memory.h>

namespace Npk::Drivers
{
    void LoadBuiltInDrivers(); //defined in drivers/BuiltInDrivers.cpp
    
    DriverManager globalDriverManager;
    DriverManager& DriverManager::Global()
    { return globalDriverManager; }

    void DriverManager::Init()
    {
        LoadBuiltInDrivers();
        Log("Driver mananger initialized.", LogLevel::Info);
    }

    void DriverManager::RegisterDriver(const DriverManifest& manifest)
    {
        sl::ScopedLock scopeLock(lock);
        
        //check name isn't already in use
        for (auto it = manifests.Begin(); it != manifests.End(); ++it)
        {
            if (it->name.length == manifest.name.length && sl::memcmp(it->name.data, manifest.name.data, it->name.length) == 0)
            {
                Log("Failed to register driver manifest, name already in use: %s", LogLevel::Error, manifest.friendlyName);
                return;
            }
        }

        manifests.PushBack(manifest);
        Log("Driver manifest registered: %s", LogLevel::Verbose, manifest.friendlyName);
    }

    bool DriverManager::TryLoadDriver(ManifestName name, InitTag* tags)
    {
        sl::ScopedLock scopeLock(lock);
        DriverManifest* manifest = nullptr;

        for (auto it = manifests.Begin(); it != manifests.End(); ++it)
        {
            if (it->name.length != name.length || sl::memcmp(it->name.data, name.data, name.length) != 0)
                continue;
            
            manifest = &*it;
        }

        if (manifest == nullptr)
            return false;
        ASSERT(manifest->builtin, "Only builtin drivers currently supported");

        using namespace Tasking;
        Thread* driverThread = Thread::Create(manifest->EnterNew, tags);
        driverThread->Start();

        return true;
    }
}
