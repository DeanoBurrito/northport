#include <drivers/DriverManager.h>
#include <debug/Log.h>
#include <tasking/Thread.h>
#include <tasking/Process.h>
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
            if (it->name == manifest.name)
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
            if (it->name != name)
                continue;
            
            manifest = &*it;
        }

        if (manifest == nullptr)
            return false;
        ASSERT(manifest->builtin, "Only builtin drivers currently supported");

        using namespace Tasking;
        auto driver = instances.EmplaceBack( *manifest, Process::Create() );
        driver.manifest = *manifest;

        Thread* mainThread = Thread::Create(manifest->EnterNew, tags, driver.proc);
        mainThread->Start();
        Log("Starting driver: %s, proc=%lu, mainThread=%lu.", LogLevel::Info, 
            manifest->friendlyName, driver.proc->Id(), mainThread->Id());

        return true;
    }

    bool DriverManager::UnloadDriver(ManifestName name, bool force)
    {
        sl::ScopedLock scopeLock(lock);
        
        for (auto it = instances.Begin(); it != instances.End(); ++it)
        {
            if (it->manifest.name != name)
                continue;
            
            Log("Stopping driver: %s, proc=%lu.", LogLevel::Debug, it->manifest.friendlyName, it->proc->Id());
            it->proc->Kill();
            instances.Erase(it);
            return true;
        }

        return false;
    }

    bool DriverManager::UnloadDriver(const char* friendlyName, bool force)
    {
        const size_t nameLen = sl::memfirst(friendlyName, 0, 0);
        sl::ScopedLock scopeLock(lock);
        
        for (auto it = instances.Begin(); it != instances.End(); ++it)
        {
            const size_t fNameLen = sl::memfirst(it->manifest.friendlyName, 0, 0);
            if (nameLen != fNameLen || sl::memcmp(friendlyName, it->manifest.friendlyName, nameLen) != 0)
                continue;
            
            Log("Stopping driver: %s, proc=%lu.", LogLevel::Debug, it->manifest.friendlyName, it->proc->Id());
            it->proc->Kill();
            instances.Erase(it);
            return true;
        }

        return false;
    }
}
