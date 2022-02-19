#pragma once

#include <stdint.h>
#include <drivers/DriverInitTags.h>

namespace Kernel::Drivers
{
    enum class DriverEventType : uint64_t
    {
        Unknown = 0,
    };

    class DriverManager;
    class DriverManifest;

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
