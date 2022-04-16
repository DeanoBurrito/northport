#pragma once

#include <devices/GenericDevice.h>

namespace Kernel::Devices::Interfaces
{
    class GenericMouse : public GenericDevice
    {
    protected: 
        virtual void Init() = 0;
        virtual void Deinit() = 0;
        
    public:
        virtual ~GenericMouse() = default;
        virtual void Reset() = 0;
        virtual sl::Opt<Drivers::GenericDriver*> GetDriverInstance() = 0;
        
        virtual size_t ButtonCount() = 0;
        virtual size_t AxisCount() = 0;
    };
}
