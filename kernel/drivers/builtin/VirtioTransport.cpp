#include <drivers/builtin/VirtioTransport.h>
#include <drivers/builtin/VirtioDefs.h>
#include <drivers/DriverManager.h>
#include <arch/Platform.h>
#include <devices/PciCapabilities.h>
#include <debug/Log.h>
#include <debug/NanoPrintf.h>
#include <interrupts/InterruptManager.h>
#include <memory/Vmm.h>
#include <memory/Pmm.h>
#include <Maths.h>
#include <Memory.h>

namespace Npk::Drivers
{
    void VirtioMmioFilterMain(void* arg)
    {
        /*
            This driver exists to handle a deficiency in the way virtio-over-mmio works:
            The virtio spec recommends that virtio devices identify themselves in the device
            tree as 'compatible="virtio,mmio"'.
            This is super unhelpful as we don't know what virtio device it is until we read
            it's registers. This driver is loaded in response to the above compat string,
            and checks the device type register to detect what driver to actually load.
            There's also the trick of a device with type 0, which means this device is
            simply a placeholder in the device tree for later on, and should be ignored.
        */
        auto maybeFilterTag = FindTag(arg, InitTagType::Filter);
        VALIDATE(maybeFilterTag,, "No filter tag");
        FilterInitTag* filterTag = static_cast<FilterInitTag*>(*maybeFilterTag);

        auto maybeDtNodeTag = FindTag(arg, InitTagType::DeviceTree);
        VALIDATE(maybeDtNodeTag,, "No device tree node tag");
        DeviceTreeInitTag* dtTag = static_cast<DeviceTreeInitTag*>(*maybeDtNodeTag);

        auto maybeReg = dtTag->node.GetProp("reg");
        VALIDATE(maybeReg,, "No regs");
        uintptr_t addr = 0;
        maybeReg->ReadRegs(dtTag->node, &addr, nullptr);

        VmObject mmio(PageSize, addr, VmFlags::Mmio);
        VALIDATE(mmio->Read<uint32_t>() == VirtioMmioMagic,, "Bad magic.");
        const uint32_t deviceType = mmio->Offset((size_t)VirtioReg::DeviceId).Read<uint32_t>();
        if (deviceType == 0)
            return; //type 0 is the placeholder device in memory maps. We're supposed to ignore it.

        const char* nameTemplate = "VirtioMmio%u";
        const size_t nameLength = npf_snprintf(nullptr, 0, nameTemplate, deviceType) + 1;
        char driverName[nameLength];
        npf_snprintf(driverName, nameLength, nameTemplate, deviceType);

        Drivers::ManifestName manifestName = { nameLength, reinterpret_cast<uint8_t*>(driverName) };
        DeviceTreeInitTag* driverInitTag = new DeviceTreeInitTag(dtTag->node, nullptr);

        *filterTag->success = Drivers::DriverManager::Global().TryLoadDriver(manifestName, driverInitTag);
        if (*filterTag->success)
            CleanupTags(arg); //Only cleanup the tags if we successfully loaded a driver.
        else
            delete driverInitTag;
    }

//signs of a bad API?
#define READ_MMIO_REG(reg) configAccess->Offset((size_t)VirtioReg::reg).Read<uint32_t>()
#define WRITE_MMIO_REG(reg, val) configAccess->Offset((size_t)VirtioReg::reg).Write<uint32_t>(val)
    
    void VirtioTransport::InitPci(Devices::PciAddress addr)
    {
        auto FindCap = [=](uint8_t type)
        {
            using namespace Devices;

            for (auto maybeCap = PciCap::Find(addr, PciCapVendor); maybeCap.HasValue(); 
                maybeCap = PciCap::Find(addr, PciCapVendor, *maybeCap))
            {
                if (!maybeCap)
                    break;
                if ((maybeCap->ReadReg(0) >> 24) != type)
                    continue;
                
                return *maybeCap;
            }
            ASSERT_UNREACHABLE();
        };
        
        using namespace Devices;
        isPci = true;
        pciAddr = addr;
        initialized = true;

        //get access to the common config structure
        const PciCap configCap = FindCap(PciCapTypeCommonCfg);
        const uintptr_t configPhysAddr = addr.ReadBar(configCap.ReadReg(1) & 0xFF, true).address + configCap.ReadReg(2);
        configAccess = VmObject { sizeof(VirtioPciCommonCfg), configPhysAddr, VmFlags::Write | VmFlags::Mmio };

        //get access to the device notification doorbells
        const PciCap notifyCap = FindCap(PciCapTypeNotifyCfg);
        const uintptr_t notifyPhysAddr = addr.ReadBar(notifyCap.ReadReg(1) & 0xFF, true).address + notifyCap.ReadReg(2);
        notifyStride = notifyCap.ReadReg(4);
        notifyAccess = VmObject{ NumQueues() * notifyStride, notifyPhysAddr, VmFlags::Write | VmFlags::Mmio };

        //get access to the device config, for later on
        const PciCap deviceCfg = FindCap(PciCapTypeDeviceCfg);
        const uintptr_t deviceCfgPhysAddr = addr.ReadBar(deviceCfg.ReadReg(1) & 0xFF, true).address + deviceCfg.ReadReg(2);
        deviceCfgAccess = VmObject { PageSize, deviceCfgPhysAddr, VmFlags::Write | VmFlags::Mmio };
    }

    void VirtioTransport::InitMmio(uintptr_t addr, size_t length)
    {
        isPci = false;
        initialized = true;

        //Everything is nicely contained in one area for MMIO devices
        configAccess = VmObject { length, addr, VmFlags::Write | VmFlags::Mmio };
        //check if device exposes legacy or modern interface.
        isLegacy = (READ_MMIO_REG(Version) == 1);

        if (isLegacy)
            WRITE_MMIO_REG(DriverPageSize, PageSize);
    }

    bool VirtioTransport::Init(InitTag* tags)
    {
        //virtio spec marks a lot of fields as litle endian. We just assume they are, maybe fix this in the future?
        static_assert(__BYTE_ORDER__ &&__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__, "Have fun with that :^)");

        bool foundTag = false;
        while (tags != nullptr && !foundTag)
        {
            if (tags->type == InitTagType::DeviceTree)
            {
                const DeviceTreeInitTag* initTag = static_cast<DeviceTreeInitTag*>(tags);
                auto maybeRegs = initTag->node.GetProp("reg");
                VALIDATE(maybeRegs, false, "No regs prop");

                uintptr_t address = 0;
                size_t length = 0;
                maybeRegs->ReadRegs(initTag->node, &address, &length);
                InitMmio(address, length);
                foundTag = true;
            }
            else if (tags->type == InitTagType::Pci)
            {
                InitPci(static_cast<PciInitTag*>(tags)->address);
                foundTag = true;
            }
            tags = tags->next;
        }

        return foundTag;
    }

    bool VirtioTransport::Deinit()
    {
        ASSERT(initialized, "Uninitialized");
        
        //cleanup any active virtqueues
        for (size_t i = 0; i < queues.Size(); i++)
        {
            if (queues[i].size == 0)
                continue;
            
            Log("VirtioTransport deinit occured while virtq %lu is active.", LogLevel::Warning, i);;
            DeinitQueue(i);
        }
        
        initialized = false;
        configAccess.Release();
        notifyAccess.Release();
        deviceCfgAccess.Release();

        return true;
    }

    void VirtioTransport::Reset()
    {
        ASSERT(initialized, "Uninitialized");

        if (isPci)
        {
            volatile VirtioPciCommonCfg* cfg = configAccess->As<volatile VirtioPciCommonCfg>();
            cfg->deviceStatus = 0;

            while (cfg->deviceStatus != 0)
                HintSpinloop();
        }
        else
        {
            WRITE_MMIO_REG(Status, 0);

            while (READ_MMIO_REG(Status) != 0)
                HintSpinloop();
        }
    }

    VirtioStatus VirtioTransport::ProgressInit(VirtioStatus stage)
    {
        ASSERT(initialized, "Uninitialized");

        if (isPci)
        {
            volatile VirtioPciCommonCfg* cfg = configAccess->As<volatile VirtioPciCommonCfg>();
            cfg->deviceStatus |= (uint8_t)stage;
            return (VirtioStatus)cfg->deviceStatus;
        }
        else
        {
            WRITE_MMIO_REG(Status, READ_MMIO_REG(Status) | (uint32_t)stage);
            return (VirtioStatus)READ_MMIO_REG(Status);
        }
    }

    uint32_t VirtioTransport::FeaturesReadWrite(size_t page, sl::Opt<uint32_t> setValue)
    {
        if (isPci)
        {
            volatile VirtioPciCommonCfg* cfg = configAccess->As<volatile VirtioPciCommonCfg>();
            if (setValue)
            {
                cfg->driverFeatureSelect = page;
                cfg->driverFeatures = *setValue;
            }
            
            cfg->deviceFeatureSelect = page;
            return cfg->deviceFeatures;
        }
        else
        {
            if (setValue)
            {
                WRITE_MMIO_REG(DriverFeaturesSel, page);
                WRITE_MMIO_REG(DriverFeatures, *setValue);
            }

            WRITE_MMIO_REG(DeviceFeaturesSel, page);
            return READ_MMIO_REG(DeviceFeatures);
        }
    }

    sl::NativePtr VirtioTransport::GetDeviceConfig()
    {
        ASSERT(initialized, "Uninitialized");

        if (isPci)
            return *deviceCfgAccess;
        else
            return configAccess->Offset((size_t)VirtioReg::DeviceConfigBase);
    }

    bool VirtioTransport::InitQueue(size_t index, uint16_t maxQueueEntries)
    {
        ASSERT(initialized, "Uninitialized");

        if (index >= NumQueues())
            return false;

        if (isPci)
        {
            volatile VirtioPciCommonCfg* cfg = configAccess->As<volatile VirtioPciCommonCfg>();
            
            cfg->queueSelect = index;
            const uint16_t queueSize = sl::Min(cfg->queueSize, maxQueueEntries);
            cfg->queueSize = queueSize;
            
            const size_t descSize = queueSize * 16; //magic numbers pulled from the virtio spec.
            const size_t availSize = queueSize * 2 + 6;
            const size_t usedSize = queueSize * 8 + 6;

            size_t totalSize = descSize;
            totalSize = sl::AlignUp(totalSize, 2);
            totalSize += availSize;
            totalSize = sl::AlignUp(totalSize, 4);
            totalSize += usedSize;

            auto maybeVector = Interrupts::InterruptManager::Global().Alloc();
            VALIDATE(maybeVector, false, "No vector.");
            auto maybeMsix = Devices::PciCap::Find(pciAddr, Devices::PciCapMsix);
            VALIDATE(maybeMsix, false, "No MSIX cap.");

            Devices::MsixCap msix(*maybeMsix);
            Devices::PciBar bir = pciAddr.ReadBar(msix.Bir());
            msixBirAccess = VmObject{ bir.size, bir.address, VmFlags::Mmio | VmFlags::Write };

            msix.SetEntry(msixBirAccess->ptr, 0, MsiAddress(CoreLocal().id, *maybeVector), 
                MsiData(CoreLocal().id, *maybeVector), false);
            msix.Enable(true);
            msix.GlobalMask(false);
            
            cfg->queueMsixVector = 0;
            if (cfg->queueMsixVector != 0)
            {
                Interrupts::InterruptManager::Global().Free(*maybeVector);
                return false;
            }

            uintptr_t queueBuff = PMM::Global().Alloc(sl::AlignUp(totalSize, PageSize) / PageSize);
            sl::memset(sl::NativePtr(queueBuff + hhdmBase).ptr, 0, totalSize);

            cfg->queueDesc = queueBuff;
            queueBuff = sl::AlignUp(queueBuff + descSize, 2);
            cfg->queueDriver = queueBuff;
            queueBuff = sl::AlignUp(queueBuff + availSize, 4);
            cfg->queueDevice = queueBuff;

            queues.EmplaceAt(index, VirtioQueue{ cfg->queueDesc, cfg->queueDriver, cfg->queueDevice, queueSize });
            cfg->queueEnable = 1;

            Log("VirtQ %lu init: size=%u, desc=0x%lx, avail=0x%lx, used=0x%lx", LogLevel::Verbose,
                index, queueSize, cfg->queueDesc, cfg->queueDriver, cfg->queueDevice);

            return true;
        }
        else
        {
            WRITE_MMIO_REG(QueueSelect, index);
            const uint16_t queueSize = sl::Min((uint16_t)READ_MMIO_REG(QueueSizeMax), maxQueueEntries);
            WRITE_MMIO_REG(QueueSize, queueSize);

            const size_t descSize = queueSize * 16;
            const size_t availSize = queueSize * 2 + 6;
            const size_t usedSize = queueSize * 8 + 6;

            size_t totalSize = descSize;
            totalSize = sl::AlignUp(totalSize, 2);
            totalSize += availSize;
            totalSize = sl::AlignUp(totalSize, isLegacy ? PageSize : 4);
            totalSize += usedSize;

            //TODO: determine interrupt routing of the device, would need to parse the device tree for this

            uintptr_t descPtr = PMM::Global().Alloc(sl::AlignUp(totalSize, PageSize) / PageSize);
            sl::memset(sl::NativePtr(descPtr + hhdmBase).ptr, 0, totalSize);

            const uintptr_t availPtr = sl::AlignUp(descPtr + descSize, 2);
            const uintptr_t usedPtr = sl::AlignUp(availPtr + availSize, isLegacy ? PageSize : 4);
            queues.EmplaceAt(index, VirtioQueue{ descPtr, availPtr, usedPtr, queueSize });

            if (isLegacy)
            {
                //legacy spec recommends page-alignment for the used ring, and qemu will break if
                //a smaller alignment is used.
                WRITE_MMIO_REG(QueueAlign, PageSize);
                WRITE_MMIO_REG(QueuePfn, descPtr / PageSize);
            }
            else
            {
                WRITE_MMIO_REG(QueueDescLow, descPtr);
                WRITE_MMIO_REG(QueueDescHigh, descPtr >> 32);
                WRITE_MMIO_REG(QueueDriverLow, availPtr);
                WRITE_MMIO_REG(QueueDriverHigh, availPtr >> 32);
                WRITE_MMIO_REG(QueueDeviceLow, usedPtr);
                WRITE_MMIO_REG(QueueDeviceHigh, usedPtr >> 32);

                WRITE_MMIO_REG(QueueReady, 1);
            }

            Log("VirtQ %lu init: %ssize=%u, desc=0x%lx, avail=0x%lx, used=0x%lx", LogLevel::Verbose,
                    index, isLegacy ? "legacy-mode, " : "", queueSize, descPtr, availPtr, usedPtr);
            return true;
        }
    }

    bool VirtioTransport::DeinitQueue(size_t index)
    {
        ASSERT(initialized, "Uninitialized");

        if (index >= NumQueues())
            return false;
        
        if (isPci)
        {
            volatile VirtioPciCommonCfg* cfg = configAccess->As<volatile VirtioPciCommonCfg>();
            cfg->queueSelect = index;
            cfg->queueEnable = 0;

            cfg->queueDesc = 0;
            cfg->queueDriver = 0;
            cfg->queueDevice = 0;

            cfg->queueMsixVector = 0xFFFF; //-1 means disable msix
            auto maybeMsix = Devices::PciCap::Find(pciAddr, Devices::PciCapMsix);
            ASSERT(maybeMsix, "No MSIX cap.");

            Devices::MsixCap msix(*maybeMsix);
            msix.GlobalMask(true);
            msix.Enable(false);
            
            uintptr_t msixAddr;
            uint32_t msixData;
            bool msixEnabled;
            msix.GetEntry(msixBirAccess->ptr, 0, msixAddr, msixData, msixEnabled);
            
            size_t ignored;
            size_t msixVector;
            MsiExtract(msixAddr, msixData, ignored, msixVector);
            Interrupts::InterruptManager::Global().Free(msixVector);
        }
        else
        {
            WRITE_MMIO_REG(QueueSelect, index);

            if (isLegacy)
            {
                WRITE_MMIO_REG(QueuePfn, 0);
            }
            else
            {
                WRITE_MMIO_REG(QueueReady, 0);

                WRITE_MMIO_REG(QueueDescLow, 0);
                WRITE_MMIO_REG(QueueDescHigh, 0);
                WRITE_MMIO_REG(QueueDriverLow, 0);
                WRITE_MMIO_REG(QueueDriverHigh, 0);
                WRITE_MMIO_REG(QueueDeviceLow, 0);
                WRITE_MMIO_REG(QueueDeviceHigh, 0);
            }
        }

        const size_t queueSize = queues[index].size;
        const uintptr_t physBase = queues[index].descTable.raw;
        queues[index].size = 0;
        queues[index].descTable = nullptr;
        queues[index].availRing = nullptr;
        queues[index].usedRing = nullptr;

        size_t sizeBytes = queueSize * 16;
        sizeBytes = sl::AlignUp(sizeBytes, 2);
        sizeBytes += queueSize * 2 + 6;
        sizeBytes = sl::AlignUp(sizeBytes, 4);
        sizeBytes += queueSize * 8 + 6;

        const size_t sizePages = sl::AlignUp(sizeBytes, PageSize) / PageSize;
        PMM::Global().Free(physBase, sizePages);

        Log("VirtQ %lu deinitialized.", LogLevel::Verbose, index);
        return true;
    }

    size_t VirtioTransport::NumQueues()
    {
        ASSERT(initialized, "Uninitialized");

        if (isPci)
        {
            volatile VirtioPciCommonCfg* cfg = configAccess->As<volatile VirtioPciCommonCfg>();
            return cfg->numQueues;
        }
        else
        {
            //test the max size of each queue, this assumes that queues are populated sequentially.
            for (size_t i = 0; i < 0xFF;i++)
            {
                WRITE_MMIO_REG(QueueSelect, i);
                if (READ_MMIO_REG(QueueSizeMax) == 0)
                    return i;
            }
            ASSERT_UNREACHABLE(); //somehow a device has more than 255 virtqueues?
        }
    }

    sl::Opt<VirtioQueue> VirtioTransport::GetQueuePtrs(size_t index)
    {
        ASSERT(initialized, "Uninitialized");

        if (index >= queues.Size())
            return {};
        return queues[index];
    }

    void VirtioTransport::NotifyDevice(size_t qIndex)
    {
        ASSERT(initialized, "Uninitialized");

        if (isPci)
        {
            volatile VirtioPciCommonCfg* cfg = configAccess->As<volatile VirtioPciCommonCfg>();
            if (qIndex >= cfg->numQueues)
                return;
            
            cfg->queueSelect = qIndex;
            if (!cfg->queueEnable)
                return;

            const size_t offset = cfg->queueNotifyOffset;
            notifyAccess->Offset(offset * notifyStride).Write<uint16_t>(qIndex);
        }
        else
            WRITE_MMIO_REG(QueueNotify, qIndex);
    }

    sl::Opt<size_t> VirtioTransport::AddDescriptor(size_t qIndex, uintptr_t base, size_t length, bool deviceWritable, sl::Opt<size_t> prev)
    {
        auto maybeQueue = GetQueuePtrs(qIndex);
        VALIDATE(maybeQueue, {}, "Bad queue index");
        VirtioQueue q = *maybeQueue;

        volatile VirtqDescriptor* descs = q.descTable.As<volatile VirtqDescriptor>(hhdmBase);
        for (size_t i = 1; i < q.size; i++)
        {
            if (descs[i].base != 0)
                continue;

            descs[i].base = base;
            descs[i].length = length;
            descs[i].flags = deviceWritable ? VirtqDescFlags::Write : VirtqDescFlags::None;
            if (prev.HasValue())
            {
                descs[*prev].next = i;
                const uint16_t flags = (uint16_t)descs[*prev].flags;
                descs[*prev].flags = (VirtqDescFlags)(flags | (uint16_t)VirtqDescFlags::Next);
            }
            
            return i;
        }

        return {};
    }

    const VirtioCmdToken VirtioTransport::BeginCmd(size_t qIndex, size_t descIndex)
    {
        auto maybeQueue = GetQueuePtrs(qIndex);
        ASSERT(maybeQueue, "Bad queue index");
        VirtioQueue q = *maybeQueue;

        volatile VirtqUsed* usedRing = q.usedRing.As<volatile VirtqUsed>(hhdmBase);
        const uint16_t submitHead = usedRing->index;

        volatile VirtqAvail* ring = q.availRing.As<volatile VirtqAvail>(hhdmBase);
        ring->ring[ring->index] = descIndex;
        ring->index = (ring->index + 1) % q.size;
        NotifyDevice(qIndex);

        return { (uint16_t)qIndex, (uint16_t)descIndex, submitHead };
    }

    size_t VirtioTransport::EndCmd(VirtioCmdToken token, bool removeDescriptors)
    {
        auto maybeQueue = GetQueuePtrs(token.queueIndex);
        ASSERT(maybeQueue, "Bad queue index");
        VirtioQueue q = *maybeQueue;

        volatile VirtqUsed* ring = q.usedRing.As<volatile VirtqUsed>(hhdmBase);
        size_t foundAt = -1ul;
        while (foundAt == -1ul)
        {
            //scan from the head (at time of cmd submission) to the current head
            for (size_t i = token.submitHead; i != ring->index; i = (i + 1) % q.size)
            {
                if (ring->ring[i].index != token.descIndex)
                    continue;
                foundAt = i;
                break;
            }
            HintSpinloop();
        }

        if (removeDescriptors)
        {
            size_t descIndex = token.descIndex;
            while (true)
            {
                volatile VirtqDescriptor* desc = &q.descTable.As<volatile VirtqDescriptor>(hhdmBase)[descIndex];
                descIndex = desc->next;
                desc->base = 0;
                desc->length = 0;
                desc->next = 0;

                if (((VirtqDescFlags)desc->flags & VirtqDescFlags::Next) != VirtqDescFlags::Next)
                    break;
            }
        }
        return ring->ring[foundAt].length;
    }
}
