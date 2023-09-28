#include <drivers/DriverManager.h>
#include <drivers/ElfLoader.h>
#include <drivers/api/Api.h>
#include <debug/Log.h>
#include <memory/Vmm.h>
#include <tasking/Thread.h>

namespace Npk::Drivers
{
    constexpr const char* LoadTypeStrs[] = 
    { 
        "never", "always", "pci-class", "pci-id",
        "dtb-compat",
    };

    bool DriverManager::LoadAndRun(sl::Handle<DriverManifest>& manifest)
    {
        VALIDATE_(manifest.Valid(), false);
        auto maybeEntry = LoadElf(VMM::Kernel(), manifest->sourcePath.Span());
        VALIDATE_(maybeEntry, false);

        auto mainFunction = reinterpret_cast<void (*)(void*)>(*maybeEntry);
        auto mainThread = Tasking::Thread::Create(mainFunction, nullptr);
        Log("Driver %s loaded, will run on thread %lu.", LogLevel::Verbose,
            manifest->friendlyName.C_Str(), mainThread->Id());

        mainThread->Start();
        return true;
    }

    DriverManager globalDriverManager;
    DriverManager& DriverManager::Global()
    { return globalDriverManager; }

    void DriverManager::Init()
    {
        Log("Driver manager initialized, api version %u.%u.%u", LogLevel::Info,
            NP_MODULE_API_VER_MAJOR, NP_MODULE_API_VER_MINOR, NP_MODULE_API_VER_REV);
    }

    bool DriverManager::Register(DriverManifest* manifest)
    {
        manifestsLock.WriterLock();

        for (auto it = manifests.Begin(); it != manifests.End(); ++it)
        {
            if ((**it)->friendlyName == manifest->friendlyName)
            {
                manifestsLock.WriterUnlock();
                Log("Failed to register driver with duplicate name: %s", LogLevel::Error, 
                    manifest->friendlyName.C_Str());
                return false;
            }
            if ((**it)->loadStr == manifest->loadStr)
            {
                manifestsLock.WriterUnlock();
                Log("Failed to register driver with duplicate load string: %s", LogLevel::Error,
                    manifest->friendlyName.C_Str());
                return false;
            }
        }

        manifests.PushBack(manifest);
        manifestsLock.WriterUnlock();

        Log("Driver manifest added: %s, module=%s, loadType=%s", LogLevel::Info, 
            manifest->friendlyName.C_Str(), manifest->sourcePath.C_Str(), 
            LoadTypeStrs[(size_t)manifest->loadType]);

        if (manifest->loadType == LoadType::Always)
        {
            sl::Handle captive(manifest);
            //still return true as we did successfully register the driver, we just failed to load it.
            VALIDATE_(LoadAndRun(captive), true);
        }
        return true;
    }

    bool DriverManager::Unregister(sl::StringSpan friendlyName)
    {
        ASSERT_UNREACHABLE();
    }

    bool DriverManager::LoadDriver(LoadType type, sl::Span<const uint8_t> loadStr)
    {
        manifestsLock.ReaderLock();

        sl::Handle<DriverManifest> manifest;
        for (auto it = manifests.Begin(); it != manifests.End(); ++it)
        {
            if ((**it)->loadType == type && (**it)->loadStr == loadStr)
            {
                manifest = *it;
                break;
            }
        }
        manifestsLock.ReaderUnlock();

        if (!manifest.Valid())
            return false;

        return LoadAndRun(manifest);
    }
}
