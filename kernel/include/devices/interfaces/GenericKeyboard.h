#pragma once

#include <devices/GenericDevice.h>

namespace Kernel::Devices
{
    class GenericKeyboard : public GenericDevice
    {
    protected:
        virtual void Init() = 0;
        virtual void Deinit() = 0;

    public:
        virtual ~GenericKeyboard() = default;
        virtual void Reset() = 0;
        virtual sl::Opt<Drivers::GenericDriver*> GetDriverInstance() = 0;
    };
}
