#pragma once

#include <stddef.h>
#include <stdint.h>
#include <Span.h>

namespace Npk::Drivers
{
    using ManifestName = sl::Span<const uint8_t>;
    using DriverEntryFunc = void (*)(void* arg);

    struct LoadedDriver;

    struct DriverManifest
    {
        ManifestName machineName;
        sl::StringSpan friendlyName;
        DriverEntryFunc EnterNew;
        LoadedDriver* control;

        constexpr DriverManifest(ManifestName mName, sl::StringSpan fName, DriverEntryFunc entry)
        : machineName(mName), friendlyName(fName), EnterNew(entry), control(nullptr)
        {}
    };
}
