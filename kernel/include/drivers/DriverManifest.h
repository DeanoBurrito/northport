#pragma once

#include <stddef.h>
#include <stdint.h>
#include <Memory.h>

namespace Npk::Drivers
{
    struct ManifestName
    {
        size_t length;
        const uint8_t* data;
    };

    constexpr bool operator==(const ManifestName& a, const ManifestName& b)
    {
        return a.length == b.length && sl::memcmp(a.data, b.data, a.length) == 0;
    }

    constexpr bool operator!=(const ManifestName& a, const ManifestName& b)
    {
        return a.length != b.length || sl::memcmp(a.data, b.data, a.length) != 0;
    }

    struct DriverManifest
    {
        bool builtin;
        bool isFilter;
        ManifestName name;
        const char* friendlyName;
        void (*EnterNew)(void* arg);
    };
}
