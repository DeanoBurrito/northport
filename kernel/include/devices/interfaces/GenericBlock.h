#pragma once

#include <devices/GenericDevice.h>
#include <devices/IoBlockBuffer.h>

namespace Kernel::Devices
{
    using BlockCmdResult = uint32_t;

    struct BlockPartition
    {
        size_t blockDeviceId;
        size_t partitionIndex;
        size_t firstLba;
        size_t lastLba;
    };

    class GenericBlock : public GenericDevice
    {
    protected:
        sl::Vector<BlockPartition> partitions;

        virtual void Init() = 0;
        virtual void Deinit() = 0;
        void PostInit() final;
    
    public:
        virtual ~GenericBlock() = default;
        virtual void Reset() = 0;
        virtual sl::Opt<Drivers::GenericDriver*> GetDriverInstance() = 0;

        virtual size_t BeginRead(size_t startLba, IoBlockBuffer& dest) = 0;
        virtual sl::Opt<BlockCmdResult> EndRead(size_t token) = 0;
        virtual size_t BeginWrite(size_t startLba, IoBlockBuffer& source) = 0;
        virtual sl::Opt<BlockCmdResult> EndWrite(size_t token) = 0;

        sl::Vector<BlockPartition> GetParts();
    };
}
