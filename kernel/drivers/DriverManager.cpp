#include <drivers/DriverManager.h>
#include <Platform.h>
#include <Memory.h>
#include <Log.h>

namespace Kernel::Drivers
{
    //NOTE: DriverManager::LoadBuiltIns() is contained in BuiltInDrivers.cpp

    DriverExtendedManifest* DriverManager::GetExtendedManifest(const DriverSubsytem subsystem, const DriverMachineName& machineName)
    {
        for (auto it = manifests->Begin(); it != manifests->End(); ++it)
        {
            if (subsystem != it->manifest.subsystem)
                continue;
            if (it->manifest.machineName.length != machineName.length)
                continue;
            
            if (sl::memcmp(it->manifest.machineName.name, machineName.name, machineName.length) == 0)
                return &(*it);
        }

        return nullptr;
    }
    
    DriverManager globalDriverManager;
    DriverManager* DriverManager::Global()
    { return &globalDriverManager; }

    void DriverManager::Init()
    {
        manifests = new sl::LinkedList<DriverExtendedManifest>;
        RegisterBuiltIns();
        //TODO: scan any mounted (and trusted) filesystems for a /drivers directory, and load any manifests contained there
        
        Logf("Kernel driver manager initialized, manifests=%u", LogSeverity::Info, manifests->Size());
    }

    void DriverManager::RegisterDriver(const DriverManifest& manifest)
    {
        ScopedSpinlock scopeLock(&lock);
        manifests->Append(manifest);
        Logf("New driver registered: name=%s, subsystem=0x%lx", LogSeverity::Verbose, manifest.name, (size_t)manifest.subsystem);
    }

    sl::Opt<DriverManifest> DriverManager::FindDriver(DriverSubsytem subsystem, DriverMachineName machineName)
    {
        ScopedSpinlock scopeLock(&lock);

        DriverExtendedManifest* extManifest = GetExtendedManifest(subsystem, machineName);

        if (extManifest == nullptr)
            return {};
        return extManifest->manifest;
    }

    bool DriverManager::StartDriver(const DriverManifest& manifest, DriverInitTag* userTags)
    {
        ScopedSpinlock scopeLock(&lock);

        DriverExtendedManifest* extManifest = GetExtendedManifest(manifest.subsystem, manifest.machineName);
        if (extManifest == nullptr)
        {
            Logf("Unable to start driver %s, matching manifest not found.", LogSeverity::Error, manifest.name);
            return false;
        }
        if (!sl::EnumHasFlag(extManifest->manifest.status, DriverStatusFlags::Loaded))
        {
            Logf("Unable to start driver %s, binary not loaded.", LogSeverity::Error, manifest.name);
            return false;
        }

        size_t index = 0;
        for (auto it = extManifest->instances.Begin(); it != extManifest->instances.End(); ++it)
        {
            if (*it == nullptr)
                break;
            index++;
        }

        DriverInitInfo initInfo;
        initInfo.id = index;
        initInfo.next = userTags; //yes, loads of security issues waiting to happen. I know :(

        void* instance = extManifest->manifest.InitNew(&initInfo);
        if (instance == nullptr)
        {
            Logf("Unable to start driver %s, instance pointer is null. Check relevent logs for more info.", LogSeverity::Error, manifest.name);
            return false;
        }

        if (index < extManifest->instances.Size())
            extManifest->instances[index] = instance;
        else
            extManifest->instances.PushBack(instance);
        
        //ensure running flag is set
        extManifest->manifest.status = sl::EnumSetFlag(extManifest->manifest.status, DriverStatusFlags::Running);
        Logf("Start new instance %u of driver %s", LogSeverity::Info, index, manifest.name);
        return true;
    }

    bool DriverManager::StopDriver(const DriverManifest& manifest, size_t instanceNumber)
    {
        ScopedSpinlock scopeLock(&lock);
        
        DriverExtendedManifest* extManifest = GetExtendedManifest(manifest.subsystem, manifest.machineName);
        if (extManifest == nullptr)
        {
            Logf("Unable to stop driver %s, matching manifest not found.", LogSeverity::Error, manifest.name);
            return false;
        }
        if (!sl::EnumHasFlag(extManifest->manifest.status, DriverStatusFlags::Running))
        {
            Logf("Unable to stop driver %s, no instances.", LogSeverity::Error, manifest.name);
            return false;
        }
        
        if (instanceNumber >= extManifest->instances.Size() || extManifest->instances[instanceNumber] == nullptr)
        {
            Logf("Unable to stop driver %s:%u, no instance with that id.", LogSeverity::Error, manifest.name, instanceNumber);
            return false;
        }

        void* instance = extManifest->instances[instanceNumber];
        extManifest->manifest.Destroy(instance);
        extManifest->instances[instanceNumber] = nullptr;
        
        //check if we were the last instance
        bool anyInstances = false;
        for (auto it = extManifest->instances.Begin(); it != extManifest->instances.End(); ++it)
        {
            if (*it != nullptr)
            {
                anyInstances = true;
                break;
            }
        }

        if (!anyInstances)
            extManifest->manifest.status = sl::EnumClearFlag(extManifest->manifest.status, DriverStatusFlags::Running);
        Logf("Stopped running instance %u of driver %s, final=%b", LogSeverity::Info, instanceNumber, manifest.name, !anyInstances);
        return true;
    }

    void DriverManager::InjectEvent(const DriverManifest& manifest, size_t instance, void* arg)
    {
        ScopedSpinlock scopeLock(&lock);
        
        DriverExtendedManifest* extManifest = GetExtendedManifest(manifest.subsystem, manifest.machineName);
        if (extManifest == nullptr)
        {
            Logf("Unable to inject event to driver %s, matching manifest not found.", LogSeverity::Error, manifest.name);
            return;
        }

        Log("Driver event injection not supported yet. TODO:", LogSeverity::Fatal);
    }
}
