#include <drivers/DriverManager.h>
#include <debug/Log.h>
#include <tasking/Thread.h>
#include <tasking/Process.h>
#include <Memory.h>

namespace Npk::Drivers
{
    void LoadBuiltInDrivers(); //defined in drivers/BuiltInDrivers.cpp

    DriverManifest* DriverManager::FindByFriendlyName(sl::StringSpan friendlyName)
    {
        for (auto it = manifests.Begin(); it != manifests.End(); ++it)
        {
            if (it->friendlyName.Size() != friendlyName.Size())
                continue;
            if (sl::memcmp(it->friendlyName.Begin(), friendlyName.Begin(), friendlyName.Size()) != 0)
                continue;
            return &*it;
        }

        return nullptr;
    }

    DriverManifest* DriverManager::FindByMachName(ManifestName machineName)
    {
        for (auto it = manifests.Begin(); it != manifests.End(); ++it)
        {
            if (it->machineName.Size() != machineName.Size())
                continue;
            if (sl::memcmp(it->machineName.Begin(), machineName.Begin(), machineName.Size()) != 0)
                continue;
            return &*it;
        }

        return nullptr;
    }

    bool DriverManager::KillDriver(LoadedDriver* driver)
    {
        driver->process->Kill();
        
        DriverManifest& manifest = driver->manifest;
        manifest.control = nullptr;
        delete driver;

        return true;
    }
    
    DriverManager globalDriverManager;
    DriverManager& DriverManager::Global()
    { return globalDriverManager; }

    void DriverManager::Init()
    {
        Log("Driver mananger initialized.", LogLevel::Info);
        LoadBuiltInDrivers();
    }

    void DriverManager::RegisterDriver(const DriverManifest& manifest)
    {
        sl::ScopedLock scopeLock(lock);
        
        //check name isn't already in use
        DriverManifest* existing = FindByMachName(manifest.machineName);
        if (existing != nullptr)
        {
            Log("Driver registration failed: %s machine name conflicts with '%s'", 
                LogLevel::Error, manifest.friendlyName.Begin(), existing->friendlyName.Begin());
            return;
        }

        manifests.PushBack(manifest);
        manifests.Back().control = nullptr;
        Log("Driver manifest registered: %s", LogLevel::Verbose, manifest.friendlyName.Begin());
    }

    bool DriverManager::TryLoadDriver(ManifestName name, InitTag* tags)
    {
        sl::ScopedLock scopeLock(lock);
        DriverManifest* manifest = FindByMachName(name);
        if (manifest == nullptr || manifest->control != nullptr)
            return false;

        manifest->control = new LoadedDriver(*manifest);
        using namespace Tasking;
        Process* proc = (manifest->control->process = Process::Create());
        Thread* mainThread = Thread::Create(manifest->EnterNew, tags, manifest->control->process);

        mainThread->Start();
        Log("Starting driver: %s, proc=%lu, mainThread=%lu.", LogLevel::Info, manifest->friendlyName.Begin(), 
            proc->Id(), mainThread->Id());

        return true;
    }

    bool DriverManager::UnloadDriver(sl::Span<const uint8_t> name)
    {
        sl::ScopedLock scopeLock(lock);
        DriverManifest* manifest = FindByMachName(name);
        if (manifest == nullptr || manifest->control == nullptr)
            return false;

        return KillDriver(manifest->control);
    }

    bool DriverManager::UnloadDriver(sl::StringSpan friendlyName)
    {
        sl::ScopedLock scopeLock(lock);
        DriverManifest* manifest = FindByFriendlyName(friendlyName);
        if (manifest == nullptr || manifest->control == nullptr)
            return false;
        
        return KillDriver(manifest->control);
    }
}
