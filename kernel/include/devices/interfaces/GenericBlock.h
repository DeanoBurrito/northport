#pragma once

#include <devices/GenericDevice.h>

namespace Kernel::Devices
{
    using BlockCmdResult = uint32_t;

    class GenericBlock : public GenericDevice
    {
    protected:
        virtual void Init() = 0;
        virtual void Deinit() = 0;
    
    public:
        virtual ~GenericBlock() = default;
        virtual void Reset() = 0;
        virtual sl::Opt<Drivers::GenericDriver*> GetDriverInstance() = 0;

        virtual size_t BeginRead(size_t startLba, sl::BufferView dest) = 0;
        virtual sl::Opt<BlockCmdResult> EndRead(size_t token) = 0;
        virtual size_t BeginWrite(size_t startLba, sl::BufferView source) = 0;
        virtual sl::Opt<BlockCmdResult> EndWrite(size_t token) = 0;
    };
}
