#include <drivers/builtin/Nvme.h>
#include <drivers/InitTags.h>
#include <arch/Platform.h>
#include <debug/Log.h>
#include <interrupts/InterruptManager.h>
#include <memory/Pmm.h>
#include <tasking/Thread.h>
#include <Maths.h>

namespace Npk::Drivers
{
    void NvmeMain(void* arg)
    {
        NvmeController driver;

        auto pciAddrTag = FindTag(arg, InitTagType::Pci);
        if (!pciAddrTag || !driver.Init(static_cast<PciInitTag*>(*pciAddrTag)->address))
        {
            Log("NVMe Controller init failed.", LogLevel::Error);
            Tasking::Thread::Current().Exit(1);
        }

        Log("NVMe init done.", LogLevel::Debug);
        while (true)
            HintSpinloop();
    }

    void NvmeController::Enable(bool yes)
    {
        auto& props = *propsAccess->As<volatile ControllerProps>();

        if (yes)
        {
            //Enable controller: the controller is not required to preserve any state
            //once the enable bit is set, so we should write all our settings at once.
            //We also leave a number of bits zeroed as well, like the arbitration mechanism
            //and page size (defaulting to 4KiB), and we select the NVM command set (0b000).

            uint32_t config = 1; //enable bit.
            config |= 6 << 16; //IOSQES: io submission queue entry size as a power of 2. 6 ^ 2 = 64 bytes.
            config |= 4 << 20; //IOCQES: completion queue entry size as power of 2. 4 ^ 2 = 16 bytes.
            props.config = config;

            while ((props.status & 1) == 0)
                HintSpinloop(); //wait for controller to signal it's initialized
            
            ASSERT((props.status & 0b10) == 0, "NVMe controller fatal error");
            Log("NVMe controller enabled.", LogLevel::Verbose);
        }
        else
        {
            //disable controller
            props.config = 0;
            while ((props.status & 1) == 1)
                HintSpinloop();
            Log("NVMe controller disabled.", LogLevel::Verbose);
        }
    }

    bool NvmeController::InitInterrupts()
    {
        using namespace Devices;
        auto intVector = Interrupts::InterruptManager::Global().Alloc();
        VALIDATE(intVector, false, "Interrupt vector allocation failed.");

        //check for MSI-X first, since it's the method preferred by the spec.
        auto maybeMsix = PciCap::Find(addr, PciCapMsix);
        if (maybeMsix)
        {
            msiCap = *maybeMsix;
            MsixCap msix = msiCap;
            msix.GlobalMask(false);
            PciBar bir = addr.ReadBar(msix.Bir());
            msixBirAccess = VmObject{ bir.size, bir.address, VmFlags::Mmio | VmFlags::Write };
            
            //mask all vectors by default
            const uintptr_t msiAddr = MsiAddress(0, *intVector);
            const uintptr_t msiData = MsiData(0, *intVector);
            for (size_t i = 0; i < msix.TableSize(); i++)
                msix.SetEntry(msixBirAccess->ptr, i, msiAddr, msiData, true);

            msix.Enable(true);
            Log("NVMe controller using MSI-X, vector %lu", LogLevel::Verbose, *intVector);
            return true;
        }

        //fallback to regular MSIs if we must
        auto maybeMsi = PciCap::Find(addr, PciCapMsi);
        if (maybeMsi)
        {
            msiCap = *maybeMsi;
            MsiCap msi = msiCap;
            msi.SetMessage(MsiAddress(0, *intVector), MsiData(0, *intVector));  
            //TODO: we should also check + set allocated vectors field.
            
            msi.Enabled(true);
            Log("NVMe controller using MSI, vector %lu", LogLevel::Verbose, *intVector);
            return true;
        }

        //I refuse to pin-based interrupts.
        Interrupts::InterruptManager::Global().Free(*intVector);
        return false;
    }

    void NvmeController::DeinitInterrupts()
    {
        ASSERT_UNREACHABLE()
    }

    bool NvmeController::CreateAdminQueue(size_t entries)
    {
        VALIDATE(queues.Size() == 0, false, "NVMe controller already has admin queue.");

        queuesLock.Lock();
        NvmeQ& queue = queues.EmplaceAt(0);
        sl::ScopedLock queueLock(queue.lock);
        queuesLock.Unlock();

        sl::NativePtr doorbellBase(propsAccess->raw + 0x1000);
        queue.entries = entries;
        queue.nextCmdId = 1;
        queue.sqTail = 0;
        queue.sqHead = 0;
        queue.cqHead = 0;

        const size_t sqBytes = entries * sizeof(SqEntry);
        const uintptr_t sqPhys = PMM::Global().Alloc(sl::AlignUp(sqBytes, PageSize));
        if (sqPhys == 0)
            return false;
        queue.sq = sl::NativePtr(sqPhys + hhdmBase).As<volatile SqEntry>();
        queue.sqDoorbell = doorbellBase.As<volatile uint32_t>();

        const size_t cqBytes = entries * sizeof(CqEntry);
        const uintptr_t cqPhys = PMM::Global().Alloc(sl::AlignUp(cqBytes, PageSize));
        if (cqPhys == 0)
            return false;
        queue.cq = sl::NativePtr(cqPhys + hhdmBase).As<CqEntry>();
        queue.cqDoorbell = doorbellBase.As<uint32_t>(doorbellStride);
        
        Log("NVMe admin queue created: sq=0x%lx, cq=0x%lx, entries=0x%lu", LogLevel::Verbose,
            (uintptr_t)sqPhys, (uintptr_t)cqPhys, entries);
        return true;
    }

    bool NvmeController::DestroyAdminQueue()
    {
        VALIDATE(queues.Size() == 1, false, "Cannot remove admin queue, other queues still exist.");

        Enable(false);

        queuesLock.Lock();
        NvmeQ q = queues.PopBack();
        queuesLock.Unlock();
        
        const size_t sqPages = sl::AlignUp(q.entries * sizeof(SqEntry), PageSize);
        PMM::Global().Free((uintptr_t)q.sq - hhdmBase, sqPages);

        const size_t cqPages = sl::AlignUp(q.entries * sizeof(CqEntry), PageSize);
        PMM::Global().Free((uintptr_t)q.cq, cqPages);

        auto& props = *propsAccess->As<volatile ControllerProps>();
        props.aqa = 0;
        props.asq = 0;
        props.acq = 0;
        
        return true;
    }

    bool NvmeController::CreateIoQueue(size_t index)
    {
        ASSERT_UNREACHABLE()
        //dont forget to unmask msix vector if we're using that
    }

    bool NvmeController::DestroyIoQueue(size_t index)
    {
        ASSERT_UNREACHABLE()
    }

    bool NvmeController::IdentifyController()
    {
        SqEntry cmd {};
        cmd.fields.opcode = 0x6; //opcode pulled from NVMe spec, under admin commands
        cmd.fields.prp1 = PMM::Global().Alloc();
        cmd.dw[10] = 1; //CNS = 1 (identify controller)
        //all other fields are fine as blank.

        auto token = BeginCmd(0, cmd);
        auto result = EndCmd(token, true);
        if (*result != 0)
        {
            LogResult(*result);
            return false;
        }

        sl::NativePtr idPtr(cmd.fields.prp1 + hhdmBase);
        //check it's an IO controller
        const size_t controllerType = idPtr.Offset(111).Read<uint8_t>();
        VALIDATE(controllerType == 1, false, "NVMe controller is wrong type (!IO)");

        //get the maximum number of bytes we're allowed to transfer per read/write op
        //this is encoded as a power of 2, where the units are the min page size of the
        //controller. Very efficient space-wish, but very confusing.
        auto& props = *propsAccess->As<volatile ControllerProps>();
        const size_t minPageSize = 0x1000 << ((props.capabilities >> 48) & 0xF);
        maxTransferSize = minPageSize * (1 << idPtr.Offset(77).Read<uint8_t>());
        if (maxTransferSize == 0)
            maxTransferSize = -1ul; //MTU == 0 means infinite, so we wrap-around.

        Log("NVMe controller: type=IO, maxTransfer=0x%lx", LogLevel::Verbose, 
            maxTransferSize);
        
        //Print some nice details like serial, model and firmware IDs.
        //These are encoded as ASCII strings, but without null terminators.
        //They're also padded with spaces to fill empty characters.
        //util function: add null terminator and trim whitespace
        auto TrimTail = [=](char* str, size_t len)
        {
            str[len] = 0;
            while (len > 0)
            {
                len--;
                if (str[len] != ' ')
                    return;
                str[len] = 0;
            }
        };

        char serial[21];
        sl::memcopy(idPtr.Offset(4).ptr, serial, 20);
        char model[41];
        sl::memcopy(idPtr.Offset(24).ptr, model, 40);
        char firmware[9];
        sl::memcopy(idPtr.Offset(64).ptr, firmware, 8);

        TrimTail(serial, 20);
        TrimTail(model, 40);
        TrimTail(firmware, 8);

        Log("NVMe controller: serial=%s, model=%s, firmware=%s", LogLevel::Verbose,
            serial, model, firmware);

        PMM::Global().Free(cmd.fields.prp1);
        return true;
    }

    void NvmeController::DiscoverNamespaces()
    {
        SqEntry cmd {};
        cmd.fields.opcode = 0x6;
        cmd.fields.prp1 = PMM::Global().Alloc();
        cmd.dw[10] = 2; //CNS = 2 (active namespace list)

        auto token = BeginCmd(0, cmd);
        auto result = EndCmd(token, true);
        if (*result != 0)
        {
            LogResult(*result);
            return;
        }

        sl::ScopedLock scopeLock(namespacesLock);
        volatile uint32_t* activeNamespaces = sl::NativePtr(cmd.fields.prp1 + hhdmBase).As<volatile uint32_t>();

        //fixed number of namespaces per identify response, id=0 means no namespace.
        for (size_t i = 0; i < 1024 && activeNamespaces[i] != 0; i++)
        {
            NvmeNamespace& ns = namespaces.EmplaceBack();
            ns.id = activeNamespaces[i];
        }

        //run identify on each namespace, gather details like format and lba count
        for (size_t i = 0; i < namespaces.Size(); i++)
        {
            NvmeNamespace& ns = namespaces[i];

            cmd.dw[10] = 0; //CNS = 0 (single namespace)
            cmd.fields.namespaceId = ns.id;

            auto token = BeginCmd(0, cmd);
            auto result = EndCmd(token, true);
            if (*result != 0)
            {
                Log("Failed to identify NVMe namespace %lu", LogLevel::Error, ns.id);
                ns.id = 0; //TODO: clean these up
                continue;
            }

            sl::NativePtr idPtr(cmd.fields.prp1 + hhdmBase);
            ns.lbaCount = idPtr.Read<uint64_t>();

            const size_t formatsCount = idPtr.Offset(25).Read<uint8_t>();
            //getting the index of the active format is a bit janky, due to 
            //protocol extension over time. Basically bit 4 is a flag, and bits 4 + 5
            //of the index are stored as bits 5 + 6 in the field.
            size_t formatIndex = idPtr.Offset(26).Read<uint8_t>() & 0xF;
            if (formatsCount > 16)
                formatIndex |= (idPtr.Offset(25).Read<uint8_t>() & 0x60) >> 1;
            
            const uint32_t activeFormat = idPtr.Offset(128 + formatIndex * 4).Read<uint32_t>();
            ns.lbaSize = 1 << ((activeFormat >> 16) & 0xFF);
            if (ns.lbaSize > 512)
            {
                Log("NVMe namespace %lu has invalid format, size %lu is too small.", 
                    LogLevel::Error, ns.id, ns.lbaSize);
                ns.id = 0;
                continue;
            }
            ASSERT((activeFormat & 0xFFFF) == 0, "NVMe driver doesn't support metadata use.");

            constexpr const char* PerfStrs[] = { "best (0)", "better (1)", "good (2)", "degraded (3)" };
            Log("Discovered NVMe NS %lu: lbaSize=%lu, lbaCount=%lu, performance=%s",
                LogLevel::Info, ns.id, ns.lbaSize, ns.lbaCount, PerfStrs[(activeFormat >> 24) & 0b11]);
        }

        PMM::Global().Free(cmd.fields.prp1);
    }

    NvmeCmdToken NvmeController::BeginCmd(size_t queueIndex, const SqEntry& cmd)
    {
        ASSERT(queueIndex < queues.Size(), "Invalid queue index");

        queuesLock.Lock();
        NvmeQ& targetQ = queues[queueIndex];
        queuesLock.Unlock();

        sl::ScopedLock queueLock(targetQ.lock);
        volatile SqEntry& slot = targetQ.sq[targetQ.sqTail++]; //TODO: check that sqTail hasn't caught up to sqHead (implying queue is full)
        for (size_t i = 0; i < 16; i++)
            slot.dw[i] = cmd.dw[i];
        
        const uint32_t cqHead = targetQ.cqHead;
        const uint16_t cmdId = targetQ.nextCmdId++;
        slot.fields.commandId = cmdId;

        if (targetQ.sqTail == targetQ.entries)
            targetQ.sqTail = 0;
        *targetQ.sqDoorbell = targetQ.sqTail;

        return { (uint16_t)queueIndex, cmdId, cqHead };
    }

    sl::Opt<NvmeResult> NvmeController::EndCmd(NvmeCmdToken token, bool block)
    {
        ASSERT(token.queueIndex < queues.Size(), "Invalid queue index");

        queuesLock.Lock();
        NvmeQ& targetQ = queues[token.queueIndex];
        queuesLock.Unlock();

        //NOTE: no need to lock here as we're only reading the completeion queue
        size_t foundAt = -1ul;
        while (foundAt == -1ul)
        {
            // for (size_t i = token.cqHead; i != targetQ.cqHead; i = (i + 1) % targetQ.entries)
            for (size_t i = 0; i < targetQ.entries; i = (i + 1) % targetQ.entries) //TODO: finish interrupt support and update cqHead
            {
                if (targetQ.cq[i].fields.commandId != token.cmdId)
                    continue;
                foundAt = i;
                break;
            }

            if (!block) //dont block, instead return that command hasn't completed
                return {};
            HintSpinloop();
        }

        //TODO: we should update cqDoorbell here to tell the controller we've read a new entry
        return targetQ.cq[foundAt].fields.status >> 1;
    }

    void NvmeController::LogResult(NvmeResult result)
    {
#ifdef NP_ALLOW_RIDICULOUS_ERROR_REPORTING
        constexpr const char* GeneralStrs[] = 
        {
            "Success", "Invalid command opcode", "Invalid field in command",
            "Command ID conflict", "Data transfer error", 
            "Commands aborted due to power loss notification",
            "Internal error", "Command abort requested", "Command aborted due to SQ deletion",
            "Command aborted due to failed fused command", "Command aborted due to missing fused command",
            "Invalid namespace or format", "Command sequence error", "Invalid SGL descriptor",
            "Invalid number of SGL descriptors", "Data SGL length invalid", "Metadata SGL length invalid",
            "SGL descriptor type invalid", "Invalid use of controller memory buffer",
            "PRP offset invalid", "Atomic write unit exceeded", "Operation denied",
            "SGL offset invalid", "<reserved error code, hope you dont see this lol>", 
            "Host identifier inconsistent format", "Keep alive timer expired", "Keep alive timeout invalid",
            "Command aborted due to preempt and abort", "Sanitize failed", "Sanitize in progress",
            "SGL data block granularity invalid", "Command not supported in queue for CMB",
            "Namespace is write protected", "Commad interrupted", "Transiet transport error",
            "Command prohibited by command and feature lockdown", "Admin command media not ready",
        };

        constexpr const char* GeneralHighStrs[] = 
        {
            "LBA out of range", "Capacity exceeded", "Namespace not ready",
            "Reservation conflict", "Format in progress", "Invalid value size",
            "Invalid key size", "KV key does not exist", "Unrecovered error",
            "Key exists"
        };
#endif
        
        const uint8_t type = (result >> 8) & 0b111;
        const uint8_t code = result & 0xFF;
        const bool dnr = (result >> 30) & 1;
        const bool more = (result >> 29) & 1;

        if (type == 0)
        {
            const uint8_t codeIndex = code < 0x80 ? code : code - 0x80;
#ifdef NP_ALLOW_RIDICULOUS_ERROR_REPORTING
            const bool isKnown = (code < 0x80 && codeIndex <= 0x25) || (code >= 0x80 && codeIndex <= 9);
#else
            constexpr bool isKnown = false;
            constexpr const char* GeneralStrs[] = {};
            constexpr const char* GeneralHighStrs[] = {};
#endif
            const char* const* table = code < 0x80 ? GeneralStrs : GeneralHighStrs;

            Log("NVMe result (General): %s, type=%u, code=%u, dnr=%u, more=%u", LogLevel::Error, 
                isKnown ? table[codeIndex] : "<unknown status>", type, code, dnr, more);
        }
        else
        {
            constexpr const char* TypeStrs[] = 
            {
                "Command-specific", "Media and data integrity error",
                "Path related status"
            };
            Log("NVMe result (%s): type=%u, code=%u, dnr=%u, more=%u", LogLevel::Error, 
                TypeStrs[type], type, code, dnr, more);
        }
    }

    bool NvmeController::Init(Devices::PciAddress pciAddr)
    {
        //Note that during init we read a few fields that are reserved in earlier
        //versions of the spec. This is fine as the spec states reserved fields
        //read as zero, and writes are ignored.
        addr = pciAddr;

        addr.MemorySpaceEnable(true);
        addr.BusMastering(true);
        addr.InterruptDisable(true);

        //for PCIe we get the address of the properties table from BAR0.
        const Devices::PciBar bar0 = addr.ReadBar(0);
        propsAccess = VmObject(bar0.size, bar0.address, VmFlags::Mmio | VmFlags::Write);

        //disable controller for now, and assert some expectations
        Enable(false);
        auto& props = *propsAccess->As<volatile ControllerProps>();
        Log("NVMe controller capabilities: 0x%lx", LogLevel::Verbose, props.capabilities);

        //check support for NVM command set
        VALIDATE(((props.capabilities >> 37) & 1) != 0, false, "NVMe controller does not support NVM command set.");
        //check controller supports native page size
        const size_t minPageSize = 0x1000 << ((props.capabilities >> 48) & 0xF);
        const size_t maxPageSize = 0x1000 << ((props.capabilities >> 52) & 0xF);
        VALIDATE(PageSize >= minPageSize && PageSize <= maxPageSize, false, 
            "NVMe controller does not support native page size.");

        //gather some data for later
        doorbellStride = 4 << ((props.capabilities >> 32) & 0xF);
        ioQueueMaxSize = (props.capabilities & 0xFFFF) + 1;

        const size_t adminEntries = sl::Min(PageSize / sizeof(SqEntry), 4096ul);
        VALIDATE(CreateAdminQueue(adminEntries), false, "CreateAdminQueue()");
        props.aqa = adminEntries | (adminEntries << 16);
        props.acq = reinterpret_cast<uintptr_t>(queues[0].cq) - hhdmBase;
        props.asq = reinterpret_cast<uintptr_t>(queues[0].sq) - hhdmBase;

        //mask interrupts before re-enabling, since msi(-x) isn't set up yet.
        props.interruptMaskSet = ~(uint32_t)0;
        //now we try to set up MSI(-X)
        VALIDATE(InitInterrupts(), false, "Failed to setup NVMe interrupts");

        //re-enable controller so we can begin submitting commands to admin queues
        Enable(true);
        Log("NVMe config: pageSize=0x%lx (0x%lx - 0x%lx), qMaxSize=%lu", 
            LogLevel::Verbose, PageSize, minPageSize, maxPageSize, ioQueueMaxSize);
        Log("NVMe version: %u.%u.%u", LogLevel::Verbose, 
        props.version >> 16, (props.version >> 8) & 0xF, props.version & 0xF);

        VALIDATE(IdentifyController(), false, "IdentifyController()");
        DiscoverNamespaces();
        //TODO: create IO queues
        //TODO: attach namespaces as block devices

        return true;
    }

    bool NvmeController::Deinit()
    {
        ASSERT_UNREACHABLE();
    }
}
