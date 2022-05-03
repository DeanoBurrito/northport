#pragma once

#include <stddef.h>
#include <Optional.h>
#include <containers/Vector.h>
#include <drivers/GenericDriver.h>

namespace Kernel::Devices
{
    enum class DeviceState : size_t
    {
        Unknown,
        Initializing,
        Ready,
        Deinitializing,
        Shutdown,
        NonAvailable,
        Error,
    };

    enum class DeviceType : size_t
    {
        GraphicsAdaptor,
        GraphicsFramebuffer,
        Keyboard,
        Mouse,

        EnumCount,
    };

    class DeviceManager;

    class GenericDevice
    {
    friend DeviceManager;
    private:
        size_t deviceId;

    protected:
        DeviceState state = DeviceState::Unknown;
        DeviceType type;
        char lock;
        //nullptr if device does not public events
        sl::Vector<size_t>* eventSubscribers = nullptr;

        virtual void Init() = 0;
        virtual void Deinit() = 0;

    public:
        virtual ~GenericDevice() = default;

        inline DeviceState GetState() const
        { return state; }

        inline size_t GetId() const
        { return deviceId; }

        inline DeviceType Type() const
        { return type; }

        virtual void Reset() = 0;
        virtual sl::Opt<Drivers::GenericDriver*> GetDriverInstance() = 0;
    };
}
