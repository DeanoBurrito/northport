#pragma once

#include <devices/GenericDevice.h>
#include <devices/interfaces/GenericFramebuffer.h>

namespace Kernel::Devices::Interfaces
{   
    class GenericGraphicsAdaptor : public GenericDevice
    {
    private:
    public:
        virtual ~GenericGraphicsAdaptor()
        {};

        virtual size_t GetFramebuffersCount() const = 0;
        virtual GenericFramebuffer* GetFramebuffer(size_t index) const = 0;
    };
}
