#pragma once

#include <stdint.h>
#include <drivers/DriverInitTags.h>

namespace Kernel::Drivers
{
    enum class DriverEventType : uint64_t
    {
        Unknown = 0,
    };

    //base class for all drivers, defines the driver API.
    class GenericDriver
    {
    protected:
    public:
        virtual ~GenericDriver() = default;

        virtual void Init(DriverInitInfo* initInfo) = 0;
        virtual void Deinit() = 0;
        virtual void HandleEvent(DriverEventType type, void* eventArg) = 0;
    };
}
