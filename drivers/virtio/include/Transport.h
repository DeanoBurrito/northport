#pragma once

#include <SpecDefs.h>
#include <interfaces/driver/Api.h>
#include <interfaces/driver/Memory.h>
#include <containers/Vector.h>
#include <PciAddress.h>
#include <VmObject.h>
#include <NativePtr.h>
#include <Optional.h>
#include <Locks.h>

namespace Virtio
{
    enum class InitPhase
    {
        None = 0,
        Acknowledge = 1 << 0,
        Driver = 1 << 1,
        DriverOk = 1 << 2,
        FeaturesOK = 1 << 3,
        NeedsReset = 1 << 6,
        Failed = 1 << 7,
    };

    constexpr InitPhase operator&(const InitPhase& a, const InitPhase& b)
    { return (InitPhase)((uintptr_t)a & (uintptr_t)b); }

    struct VirtqPtrs
    {
        VirtqDescEntry* descs;
        VirtqAvailable* avail;
        VirtqUsed* used;
        uint16_t size;
        uint16_t availHead;
        size_t notifyOffset;

        sl::SpinLock descLock;
        sl::SpinLock ringLock;
    };

    class Transport
    {
    private:
        dl::PciAddress pciAddr;
        dl::VmObject configAccess;
        dl::VmObject notifyAccess;
        dl::VmObject deviceCfgAccess;
        uint32_t notifyStride;

        bool isPciTransport;
        bool isLegacy;
        bool initialized = false;

        sl::Vector<VirtqPtrs> virtqPtrs;

        bool SetupPciAccess(dl::VmObject& vmo, uint8_t type);
        bool InitPci(npk_handle descriptorId);
        bool InitMmio(uintptr_t regsBase);

        sl::Opt<uint16_t> AllocDesc(size_t queue);
        void FreeDesc(size_t queue, uint16_t index);
        void NotifyDevice(size_t qIndex);

    public:
        bool Init(npk_event_add_device* event);
        bool Shutdown();

        bool Reset();
        bool ProgressInit(InitPhase phase);
        sl::NativePtr DeviceConfig();
        size_t MaxQueueCount();
        uint32_t Features(size_t page, sl::Opt<uint32_t> setValue = {});

        bool CreateQueue(size_t index, uint16_t maxEntries);
        bool DestroyQueue(size_t index);
        bool NotificationsEnabled(size_t index, sl::Opt<bool> yes = {});

        sl::Opt<uint16_t> PrepareCommand(size_t q, sl::Span<npk_mdl> mdls);
        bool BeginCommand(size_t q, uint16_t id);
        bool EndCommand(size_t q, uint16_t id);
    };
}
