#pragma once

#include <stddef.h>
#include <Locks.h>

namespace Npk::Devices
{
    enum class DeviceType : size_t
    {
        Keyboard = 0,
        Pointer = 1,
    };

    enum class DeviceStatus
    {
        Offline,
        Starting,
        Online,
        Stopping,
        Error,
    };

    class DeviceManager;

    class GenericDevice
    {
    friend DeviceManager;
    private:
        DeviceType type;
        size_t id;

    protected:
        DeviceStatus status;
        sl::TicketLock lock;
    
    public:
        GenericDevice(DeviceType type) : type(type)
        {}

        [[gnu::always_inline]]
        inline DeviceType Type()
        { return type; }

        [[gnu::always_inline]]
        inline size_t Id()
        { return id; }

        virtual bool Init() = 0;
        virtual bool Deinit() = 0;
    };
}
