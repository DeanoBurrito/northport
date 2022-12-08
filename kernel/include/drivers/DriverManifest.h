#pragma once

#include <stddef.h>
#include <stdint.h>

namespace Npk::Drivers
{
    struct ManifestName
    {
        size_t length;
        const uint8_t* data;
    };

    class GenericDriver;
    
    struct DriverManifest
    {
        bool builtin;
        ManifestName name;
        const char* friendlyName;
        void (*EnterNew)(void* arg);
    };
}
