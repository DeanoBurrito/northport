#include <Transport.h>
#include <SpecDefs.h>
#include <Log.h>
#include <PciCapabilities.h>
#include <ArchHints.h>

#define MMIO_READ(reg) sl::LeToHost(configAccess->Offset(static_cast<unsigned>(MmioReg::reg)).Read<uint32_t>())
#define MMIO_WRITE(reg, val) configAccess->Offset(static_cast<unsigned>(MmioReg::reg)).Write<uint32_t>(sl::HostToLe((uint32_t)(val)))

namespace Virtio
{
    bool Transport::SetupPciAccess(dl::VmObject& vmo, uint8_t type)
    {
        uint8_t capOffset = 0;

        auto maybeCap = dl::FindPciCapability(pciAddr, dl::PciCapabilityType::Vendor);
        while (maybeCap.HasValue())
        {
            if ((pciAddr.Read(*maybeCap) >> 24) == type)
            {
                capOffset = *maybeCap;
                break;
            }
            maybeCap = dl::FindPciCapability(pciAddr, dl::PciCapabilityType::Vendor, *maybeCap);
        }
        VALIDATE(capOffset != 0, false, "Failed to find pci vendor cap with correct type.");

        const dl::PciBar bar = pciAddr.ReadBar(pciAddr.Read(capOffset + 4) & 0xFF);
        const size_t offset = pciAddr.Read(capOffset + 8);
        const size_t length = pciAddr.Read(capOffset + 12);

        if (type == NotifyConfigCap)
        {
            notifyStride = sl::LeToHost(pciAddr.Read(capOffset + 16));
            Log("Notify stride is %" PRIu32, LogLevel::Verbose, notifyStride);
        }

        VALIDATE(bar.isMemory, false, "Only memory BARs are currently supported");
        const uintptr_t paddr = bar.base + offset;
        vmo = dl::VmObject(length, paddr, dl::VmFlag::Mmio | dl::VmFlag::Write);
        VALIDATE_(vmo.Valid(), false);

        return true;
    }

    bool Transport::InitPci(npk_handle descriptorId)
    {
        auto FindCap = [=](uint8_t type) -> uint8_t
        {
            auto maybeCap = dl::FindPciCapability(pciAddr, dl::PciCapabilityType::Vendor);
            while (maybeCap.HasValue())
            {
                if ((pciAddr.Read(*maybeCap) >> 24) == type)
                    return *maybeCap;
                maybeCap = dl::FindPciCapability(pciAddr, dl::PciCapabilityType::Vendor, *maybeCap);
            }
            return 0;
        };

        pciAddr = dl::PciAddress(descriptorId);
        isPciTransport = true;
        isLegacy = false;

        VALIDATE_(SetupPciAccess(configAccess, CommonConfigCap), false);
        VALIDATE_(SetupPciAccess(notifyAccess, NotifyConfigCap), false);
        VALIDATE_(SetupPciAccess(deviceCfgAccess, DeviceConfigCap), false);

        initialized = true;
        Log("PCI transport init: cfg=%p, notify=%p, devCfg=%p", LogLevel::Info,
            configAccess->ptr, notifyAccess->ptr, deviceCfgAccess->ptr);
        return true;
    }

    bool Transport::InitMmio(uintptr_t regsBase)
    {
        ASSERT_UNREACHABLE();
    }

    sl::Opt<uint16_t> Transport::AllocDesc(size_t queue)
    { 
        auto& queueData = virtqPtrs[queue];
        sl::ScopedLock scopeLock(queueData.descLock); //TODO: use next field as a freelist instead of this

        for (uint16_t i = 1; i < queueData.size; i++)
        {
            if (queueData.descs[i].base != 0)
                continue;

            queueData.descs[i].base = 0;
            return i;
        }

        return {}; 
    }

    void Transport::FreeDesc(size_t queue, uint16_t index)
    {
        auto& queueData = virtqPtrs[queue];
        sl::ScopedLock scopeLock(queueData.descLock);
        while (true)
        {
            queueData.descs[index].base = 0;
            if (queueData.descs[index].flags.Load() & (uint16_t)VirtqDescFlags::Next)
                index = queueData.descs[index].next;
            else
                break;
        }
    }

    void Transport::NotifyDevice(size_t qIndex)
    {
        VALIDATE_(initialized, );

        sl::DmaWriteBarrier();
        if (isPciTransport)
            notifyAccess->Offset(virtqPtrs[qIndex].notifyOffset).Write<uint16_t>(qIndex);
        else
            MMIO_WRITE(QueueNotify, qIndex);
    }

    static void HandleTransportIntr(void* arg)
    { static_cast<Transport*>(arg)->HandleInterrupt(); }

    bool Transport::Init(npk_event_add_device* event)
    {
        ASSERT_(event->descriptor_id != NPK_INVALID_HANDLE); //TODO: support for virtio over mmio

        VALIDATE_(InitPci(event->descriptor_id), false);

        //generic device init, we go as far as setting feature bits
        Reset();
        VALIDATE_(ProgressInit(InitPhase::Acknowledge), false);
        VALIDATE_(ProgressInit(InitPhase::Driver), false);

        //at the time of writing (virtio v1.2) all bits > 128 are reserved,
        //so we dont bother with them.
        const uint32_t features[4] = { Features(0), Features(1), Features(2), Features(3) };
        Log("Device features: 0x%" PRIx32", 0x%" PRIx32", 0x%" PRIx32", 0x%" PRIx32,
            LogLevel::Verbose, features[0], features[1], features[2], features[3]);

        intrRoute.dpc = &intrDpc;
        intrDpc.function = HandleTransportIntr;
        intrDpc.arg = this;
        VALIDATE_(npk_add_interrupt_route(&intrRoute, NPK_NO_AFFINITY), false);

        return true;
    }

    bool Transport::Shutdown()
    {
        ASSERT_UNREACHABLE();
    }

    void Transport::HandleInterrupt()
    {
        Log("VIRTIO interrupt", LogLevel::Debug);
    }

    bool Transport::Reset()
    {
        ASSERT_(initialized);

        if (isPciTransport)
        {
            auto cfg = configAccess->As<volatile PciCommonCfg>();
            cfg->status = 0;

            while (cfg->status != 0)
                sl::HintSpinloop();

            return cfg->status == 0; //TODO: can this op fail at all?
        }
        else
        {
            MMIO_WRITE(Status, 0);

            while (MMIO_READ(Status) != 0)
                sl::HintSpinloop();

            return MMIO_READ(Status) == 0;
        }
    }

    bool Transport::ProgressInit(InitPhase phase)
    {
        ASSERT_(initialized)
        
        if (isPciTransport)
        {
            auto cfg = configAccess->As<volatile PciCommonCfg>();
            const uint8_t expected = cfg->status | static_cast<uint8_t>(phase);
            cfg->status = expected;
            
            return cfg->status == expected; //check the bit remains set and no other bits were set (NeedsReset)
        }
        else
        {
            const uint8_t expected = MMIO_READ(Status) | static_cast<uint8_t>(phase);
            MMIO_WRITE(Status, expected);
            
            return MMIO_READ(Status) == expected;
        }
    }

    sl::NativePtr Transport::DeviceConfig()
    {
        ASSERT_(initialized);

        if (isPciTransport)
            return *deviceCfgAccess;
        else
            return configAccess->Offset(static_cast<unsigned>(MmioReg::DeviceConfigBase));
    }

    size_t Transport::MaxQueueCount()
    {
        ASSERT_(initialized);

        if (isPciTransport)
            return configAccess->As<volatile PciCommonCfg>()->numQueues;
        else
        {
            for (size_t i = 0; true; i++)
            {
                MMIO_WRITE(QueueSelect, i);
                if (MMIO_READ(QueueSizeMax) == 0)
                    return i;
            }
        }
    }

    uint32_t Transport::Features(size_t page, sl::Opt<uint32_t> setValue)
    {
        ASSERT_(initialized);

        if (isPciTransport)
        {
            auto cfg = configAccess->As<volatile PciCommonCfg>();
            if (setValue.HasValue())
            {
                cfg->driverFeatureSelect = page;
                cfg->driverFeatures = *setValue;
            }

            cfg->deviceFeatureSelect = page;
            return cfg->deviceFeatures;
        }
        else
        {
            if (setValue.HasValue())
            {
                MMIO_WRITE(DriverFeaturesSelect, page);
                MMIO_WRITE(DriverFeatures, *setValue);
            }

            MMIO_WRITE(DeviceFeatures, page);
            return MMIO_READ(DeviceFeatures);
        }
    }

    bool Transport::CreateQueue(size_t index, uint16_t maxEntries)
    {
        ASSERT_(initialized);

        VALIDATE_(index < MaxQueueCount(), false);
        VALIDATE_(index >= virtqPtrs.Size() || virtqPtrs[index].descs == nullptr, false);

        uint16_t queueSize = 0;
        if (isPciTransport)
        {
            auto cfg = configAccess->As<volatile PciCommonCfg>();
            cfg->queueSelect = index;

            queueSize = sl::Min(cfg->queueSize.Load(), maxEntries);
            cfg->queueSize = queueSize;

            auto maybeMsi = dl::FindMsi(pciAddr);
            VALIDATE_(maybeMsi.HasValue(), false);
            npk_msi_config msiConfig {};
            VALIDATE_(npk_construct_msi(&intrRoute, &msiConfig), false);
            Log("Using MSI%s for interrupts: addr=0x%tx, data=0x%tx", LogLevel::Verbose,
                maybeMsi->IsMsix() ? "-X" : "", msiConfig.address, msiConfig.data);

            for (size_t i = 0; i < maybeMsi->VectorCount(); i++)
                maybeMsi->SetVector(i, msiConfig.address, msiConfig.data, false);
            maybeMsi->Enable();

            cfg->queueMsixVector = 0;
            VALIDATE_(cfg->queueMsixVector.Load() != 0xFFFF, false);
        }
        else
        {
            MMIO_WRITE(QueueSelect, index);
            queueSize = sl::Min(static_cast<uint16_t>(MMIO_READ(QueueSizeMax)), maxEntries);
            MMIO_WRITE(QueueSize, queueSize);

            //TODO: MMIO transport interrupt routing
            Log("VIRTIO is broken on mmio for now, no interrupts fired", LogLevel::Warning);
        }
        const size_t pageSize = npk_pm_alloc_size();
        const size_t usedRingAlign = isLegacy ? pageSize : 4; //legacy spec recommend page alignment for used ring

        const size_t descSize = queueSize * 16; //magic numbers are pulled from the virtio spec
        const size_t availSize = queueSize * 2 + 6;
        const size_t usedSize = queueSize * 8 + 6;
        const size_t totalSize = [=]()
        {
            size_t accum = descSize;
            accum = sl::AlignUp(accum, 2);
            accum += availSize;
            accum = sl::AlignUp(accum, usedRingAlign);
            accum += usedSize;
            return accum;
        }();

        const uintptr_t queueBuff = npk_pm_alloc_many(sl::AlignUp(totalSize, pageSize) / pageSize, nullptr);
        VALIDATE_(queueBuff != 0, false);
        sl::memset(reinterpret_cast<void*>(queueBuff + npk_hhdm_base()), 0, totalSize);

        const uintptr_t availBuff = sl::AlignUp(queueBuff + descSize, 2);
        const uintptr_t usedBuff = sl::AlignUp(availBuff + availSize, usedRingAlign);
        if (isPciTransport)
        {
            auto cfg = configAccess->As<volatile PciCommonCfg>();
            cfg->queueDesc = queueBuff;
            cfg->queueDriver = availBuff;
            cfg->queueDevice = usedBuff;

            cfg->queueEnable = 1;
        }
        else
        {
            if (isLegacy)
            {
                MMIO_WRITE(QueueAlign, pageSize);
                MMIO_WRITE(QueuePfn, queueBuff / pageSize);
            }
            else
            {
                MMIO_WRITE(QueueDescLow, queueBuff);
                MMIO_WRITE(QueueDescHigh, queueBuff >> 32);
                MMIO_WRITE(QueueDriverLow, availBuff);
                MMIO_WRITE(QueueDriverHigh, availBuff >> 32);
                MMIO_WRITE(QueueDeviceLow, usedBuff);
                MMIO_WRITE(QueueDeviceHigh, usedBuff >> 32);

                MMIO_WRITE(QueueReady, 1);
            }
        }

        const size_t hhdmBase = npk_hhdm_base();
        auto& ptrs = virtqPtrs.EmplaceAt(index);
        ptrs.descs = sl::NativePtr(queueBuff + hhdmBase).As<VirtqDescEntry>();
        ptrs.avail = sl::NativePtr(availBuff + hhdmBase).As<VirtqAvailable>();
        ptrs.used = sl::NativePtr(usedBuff + hhdmBase).As<VirtqUsed>();
        ptrs.size = queueSize;

        ptrs.notifyOffset = 0;
        if (isPciTransport)
        {
            auto cfg = configAccess->As<volatile PciCommonCfg>();
            ptrs.notifyOffset = notifyStride * cfg->queueNotifyOffset;
        }

        Log("VirtQ %zu init: size=%u, desc=0x%tx, avail=0x%tx, used=0x%tx", LogLevel::Verbose,
            index, queueSize, queueBuff, availBuff, usedBuff);
        return true;
    }

    bool Transport::DestroyQueue(size_t index)
    {
        ASSERT_UNREACHABLE();
    }

    bool Transport::NotificationsEnabled(size_t index, sl::Opt<bool> yes)
    {
        ASSERT_UNREACHABLE();
    }

    sl::Opt<uint16_t> Transport::PrepareCommand(size_t q, sl::Span<npk_mdl> mdls)
    {
        VALIDATE_(q < virtqPtrs.Size(), {});

        uint16_t firstDesc = -1;
        uint16_t prevDesc;
        for (size_t m = 0; m < mdls.Size(); m++)
        {
            for (size_t i = 0; i < mdls[m].ptr_count; i++)
            {
                auto maybeDesc = AllocDesc(q);
                if (!maybeDesc.HasValue())
                {
                    Log("Failed to allocate descriptor slot.", LogLevel::Error);
                    if (firstDesc != (uint16_t)-1)
                        FreeDesc(q, firstDesc);
                    return {};
                }

                volatile VirtqDescEntry* desc = &virtqPtrs[q].descs[*maybeDesc];
                desc->base = mdls[m].ptrs[i].phys_base;
                desc->length = mdls[m].ptrs[i].length;
                desc->flags = 0;
                if (m != 0)
                    desc->flags = (uint16_t)VirtqDescFlags::Write; //TODO: proper interface for setting device portion of buffer

                if (firstDesc == (uint16_t)-1)
                    firstDesc = *maybeDesc;
                else
                {
                    volatile VirtqDescEntry* prev = &virtqPtrs[q].descs[prevDesc];
                    prev->flags = prev->flags | (uint16_t)VirtqDescFlags::Next;
                    prev->next = *maybeDesc;
                }

                prevDesc = *maybeDesc;
            }
        }

        if (firstDesc == (uint16_t)-1)
            return {};
        return firstDesc;
    }

    bool Transport::BeginCommand(CommandHandle& cmd)
    {
        VALIDATE_(cmd.queueIndex < virtqPtrs.Size(), false);
        VALIDATE_(cmd.descId < virtqPtrs[cmd.queueIndex].size, false);

        //npk_reset_event(&cmd.completed);

        sl::ScopedLock scopeLock(virtqPtrs[cmd.queueIndex].ringLock);
        volatile VirtqAvailable* avail = virtqPtrs[cmd.queueIndex].avail;
        cmd.usedStartId = virtqPtrs[cmd.queueIndex].used->index;

        sl::DmaWriteBarrier();
        const uint16_t availIndex = avail->index;
        avail->ring[availIndex] = cmd.descId;
        avail->index = (availIndex + 1) % virtqPtrs[cmd.queueIndex].size;

        NotifyDevice(cmd.queueIndex);

        return true;
    }

    bool Transport::EndCommand(CommandHandle& cmd, size_t timeoutNs)
    {
        VALIDATE_(cmd.queueIndex < virtqPtrs.Size(), false);
        VALIDATE_(cmd.descId< virtqPtrs[cmd.queueIndex].size, false);

        auto& q = virtqPtrs[cmd.queueIndex];
        bool found = false;
        volatile VirtqUsedElement* ring = q.used->ring;

        sl::ScopedLock scopeLock(q.ringLock);
        while (!found)
        {
            for (size_t i = cmd.usedStartId; i != ring->index; i = (i + 1) % q.size)
            {
                if (ring[i].index != cmd.descId)
                    continue;

                found = true;
                break;
            }

            if (timeoutNs == -1)
                continue;

            npk_wait_entry waitEntry {};
            npk_duration timeout {};
            timeout.ticks = timeoutNs;
            timeout.scale = npk_time_scale::Nanos;
            if (!npk_wait_one(&cmd.completed, &waitEntry, timeout))
                return false; //event not fired before timeout, meaning we couldnt complete the operation
        }

        FreeDesc(cmd.queueIndex, cmd.descId);
        return true;
    }
}
