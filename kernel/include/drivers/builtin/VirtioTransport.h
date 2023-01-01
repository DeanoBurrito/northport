#pragma once

#include <devices/PciAddress.h>
#include <drivers/InitTags.h>
#include <memory/VmObject.h>
#include <containers/Vector.h>

namespace Npk::Drivers
{
    void VirtioMmioFilterMain(void* arg);
    
    enum class VirtioStatus : uint8_t
    {
        None = 0,
        Acknowledge = 1,
        Driver = 2,
        Failed = 128,
        FeaturesOk = 8,
        DriverOk = 4,
        DeviceNeedReset = 64,
    };

    constexpr VirtioStatus operator&(const VirtioStatus& a, const VirtioStatus& b)
    { return (VirtioStatus)((uintptr_t)a & (uintptr_t)b); }

    struct VirtioQueue
    {
        sl::NativePtr descTable;
        sl::NativePtr availRing;
        sl::NativePtr usedRing;
        size_t size = 0;
    };

    struct VirtioCmdToken
    {
        uint16_t queueIndex; //virtq the command was submitted to
        uint16_t descIndex; //index we're waiting for in the used ring
        uint16_t submitHead; //head of used ring when cmd was submitted.
    };

    class VirtioTransport
    {
    private:
        Devices::PciAddress pciAddr;
        VmObject configAccess;
        VmObject notifyAccess;
        VmObject deviceCfgAccess;

        sl::Vector<VirtioQueue> queues;

        size_t notifyStride;
        bool isPci;
        bool isLegacy;
        bool initialized = false;
        
        void InitPci(Devices::PciAddress addr);
        void InitMmio(uintptr_t addr, size_t length);

    public:
        bool Init(InitTag* tags);
        bool Deinit();
        
        void Reset();
        VirtioStatus ProgressInit(VirtioStatus stage);
        uint32_t FeaturesReadWrite(size_t page, sl::Opt<uint32_t> setValue = {});
        sl::NativePtr GetDeviceConfig();

        bool InitQueue(size_t index, uint16_t maxQueueEntries);
        bool DeinitQueue(size_t index);
        size_t NumQueues();
        sl::Opt<VirtioQueue> GetQueuePtrs(size_t index);
        void NotifyDevice(size_t qIndex);

        //not transport specific, but operations common to all virtio drivers.
        sl::Opt<size_t> AddDescriptor(size_t qIndex, uintptr_t base, size_t length, bool deviceWritable, sl::Opt<size_t> prev = {});
        const VirtioCmdToken BeginCmd(size_t qIndex, size_t descIndex);
        size_t EndCmd(const VirtioCmdToken token, bool removeDescriptors);
    };
}
