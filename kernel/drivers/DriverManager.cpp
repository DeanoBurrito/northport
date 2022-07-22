#include <drivers/DriverManager.h>
#include <Platform.h>
#include <Memory.h>
#include <Log.h>
#include <Locks.h>

namespace Kernel::Drivers
{
    constexpr const char* SubsystemStrs[] =
    {
        "misc",
        "pci",
        "filesystem",
    };

    //NOTE: DriverManager::LoadBuiltIns() is contained in BuiltInDrivers.cpp

    DriverExtendedManifest* DriverManager::GetExtendedManifest(const DriverSubsystem subsystem, const DriverMachineName& machineName)
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
        
        Logf("Kernel driver manager initialized, manifests=%u", LogSeverity::Info, manifests->Size());
    }

    void DriverManager::RegisterDriver(const DriverManifest& manifest)
    {
        sl::ScopedSpinlock scopeLock(&lock);
        manifests->Append(manifest);
        Logf("Added driver manifest: subsystem=%u (%s), name=%s", LogSeverity::Verbose, 
            (size_t)manifest.subsystem, SubsystemStrs[(size_t)manifest.subsystem], manifest.name);
    }

    sl::Opt<DriverManifest*> DriverManager::FindDriver(DriverSubsystem subsystem, DriverMachineName machineName)
    {
        sl::ScopedSpinlock scopeLock(&lock);

        DriverExtendedManifest* extManifest = GetExtendedManifest(subsystem, machineName);

        if (extManifest == nullptr)
            return {};
        return &extManifest->manifest;
    }

    bool DriverManager::StartDriver(const DriverManifest* manifest, DriverInitTag* userTags)
    {
        sl::ScopedSpinlock scopeLock(&lock);

        DriverExtendedManifest* extManifest = GetExtendedManifest(manifest->subsystem, manifest->machineName);
        if (extManifest == nullptr)
        {
            Logf("Unable to start driver %s, matching manifest not found.", LogSeverity::Error, manifest->name);
            return false;
        }
        if (!sl::EnumHasFlag(extManifest->manifest.status, DriverStatusFlags::Loaded))
        {
            Logf("Unable to start driver %s, binary not loaded.", LogSeverity::Error, manifest->name);
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

        GenericDriver* instance = extManifest->manifest.CreateNew();
        if (instance == nullptr)
        {
            Logf("Unable to start driver %s, instance pointer is null. Check relevent logs for more info.", LogSeverity::Error, manifest->name);
            return false;
        }
        
        instance->manifest = &extManifest->manifest;
        instance->Init(&initInfo);

        if (index < extManifest->instances.Size())
            extManifest->instances[index] = instance;
        else
            extManifest->instances.PushBack(instance);
        
        //ensure running flag is set
        extManifest->manifest.status = sl::EnumSetFlag(extManifest->manifest.status, DriverStatusFlags::Running);
        Logf("New driver started: %s (instance %u).", LogSeverity::Info, manifest->name, index);
        return true;
    }

    bool DriverManager::StopDriver(const DriverManifest* manifest, size_t instanceNumber)
    {
        sl::ScopedSpinlock scopeLock(&lock);
        
        DriverExtendedManifest* extManifest = GetExtendedManifest(manifest->subsystem, manifest->machineName);
        if (extManifest == nullptr)
        {
            Logf("Unable to stop driver %s, matching manifest not found.", LogSeverity::Error, manifest->name);
            return false;
        }
        if (!sl::EnumHasFlag(extManifest->manifest.status, DriverStatusFlags::Running))
        {
            Logf("Unable to stop driver %s, no instances.", LogSeverity::Error, manifest->name);
            return false;
        }
        if (instanceNumber >= extManifest->instances.Size() || extManifest->instances[instanceNumber] == nullptr)
        {
            Logf("Unable to stop driver %s:%u, invalid instance id.", LogSeverity::Error, manifest->name, instanceNumber);
            return false;
        }

        GenericDriver* instance = extManifest->instances[instanceNumber];
        instance->Deinit();
        delete instance;
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
        Logf("Stopped insance %u of driver %s, final=%b", LogSeverity::Info, instanceNumber, manifest->name, !anyInstances);
        return true;
    }

    void DriverManager::InjectEvent(const DriverManifest* manifest, size_t instanceNumber, DriverEventType type, void* arg)
    {
        sl::ScopedSpinlock scopeLock(&lock);
        
        DriverExtendedManifest* extManifest = GetExtendedManifest(manifest->subsystem, manifest->machineName);
        if (extManifest == nullptr)
        {
            Logf("Failed to inject driver event: %s, matching manifest not found.", LogSeverity::Error, manifest->name);
            return;
        }
        if (!sl::EnumHasFlag(extManifest->manifest.status, DriverStatusFlags::Running))
        {
            Logf("Failed to inject driver event: %s, no instances.", LogSeverity::Error, manifest->name);
            return;
        }
        
        if (instanceNumber >= extManifest->instances.Size() || extManifest->instances[instanceNumber] == nullptr)
        {
            Logf("Failed to inject driver event: %s:%u, invalid id", LogSeverity::Error, manifest->name, instanceNumber);
            return;
        }

        GenericDriver* instance = extManifest->instances[instanceNumber];
        instance->HandleEvent(type, arg);
    }

    GenericDriver* DriverManager::GetDriverInstance(const DriverManifest* manifest, size_t instanceNumber)
    {
        sl::ScopedSpinlock scopeLock(&lock);
        
        DriverExtendedManifest* extManifest = GetExtendedManifest(manifest->subsystem, manifest->machineName);
        if (extManifest == nullptr)
        {
            Logf("Unable to get driver instance, manifest not found for name %s.", LogSeverity::Error, manifest->name);
            return nullptr;
        }

        if (instanceNumber >= extManifest->instances.Size())
        {
            Log("Unable to get driver instance, instance id too high!", LogSeverity::Error);
            return nullptr;
        }

        return extManifest->instances[instanceNumber];
    }
}
