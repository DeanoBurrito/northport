#pragma once

#include <drivers/DriverInitTags.h>
#include <drivers/DriverEventType.h>

namespace Kernel::Drivers
{
    class DriverManager;
    struct DriverManifest;

    //base class for all drivers, defines the driver API.
    class GenericDriver
    {
    friend DriverManager;
    private:
        DriverManifest* manifest;
    protected:
    public:
        virtual ~GenericDriver() = default;

        [[gnu::always_inline]] inline 
        const DriverManifest* Manifest() const
        { return manifest; }

        virtual void Init(DriverInitInfo* initInfo) = 0;
        virtual void Deinit() = 0;
        virtual void HandleEvent(DriverEventType type, void* eventArg) = 0;
    };
}
