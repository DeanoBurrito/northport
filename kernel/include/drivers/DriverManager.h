#pragma once

#include <String.h>
#include <Handle.h>
#include <Locks.h>
#include <containers/LinkedList.h>

namespace Npk::Drivers
{
    enum class LoadType
    {
        Never = 0,
        Always = 1,
        PciClass = 2,
        PciId = 3,
        DtbCompat = 4,
    };

    struct DriverManifest
    {
        size_t references;
        sl::String sourcePath;
        sl::String friendlyName;

        LoadType loadType;
        sl::Span<const uint8_t> loadStr;
    };

    class DriverManager
    {
    private:
        sl::RwLock manifestsLock;
        sl::LinkedList<sl::Handle<DriverManifest>> manifests;

        bool LoadAndRun(sl::Handle<DriverManifest>& manifest);

    public:
        static DriverManager& Global();

        void Init();
        bool Register(DriverManifest* manifest);
        bool Unregister(sl::StringSpan friendlyName);
        bool LoadDriver(LoadType type, sl::Span<const uint8_t> loadStr);
    };
}
