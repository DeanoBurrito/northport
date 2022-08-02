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
        Block,

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
        char lock = 0;
        //nullptr if device does not public events
        sl::Vector<size_t>* eventSubscribers = nullptr;

        virtual void Init() = 0;
        virtual void Deinit() = 0;
        virtual void PostInit();

    public:
        virtual ~GenericDevice() = default;

        inline DeviceState GetState() const
        { return state; }

        inline size_t GetId() const
        { return deviceId; }

        inline DeviceType Type() const
        { return type; }

        //functions required to be implemented by derived devices
        virtual void Reset() = 0;
        virtual sl::Opt<Drivers::GenericDriver*> GetDriverInstance() = 0;

        //optional functions for derivatives
        virtual void EventPump();
    };
}
