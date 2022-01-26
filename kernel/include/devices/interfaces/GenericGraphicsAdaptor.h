#pragma once

#include <devices/interfaces/GenericFramebuffer.h>

namespace Kernel::Devices::Interfaces
{   
    class GenericGraphicsAdaptor
    {
    private:
    public:
        virtual ~GenericGraphicsAdaptor()
        {};

        virtual void Init() = 0;
        virtual void Destroy() = 0;

        virtual size_t GetFramebuffersCount() const = 0;
        virtual GenericFramebuffer* GetFramebuffer(size_t index) const = 0;
    };
}
