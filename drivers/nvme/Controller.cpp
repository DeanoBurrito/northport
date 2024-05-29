#include <Controller.h>
#include <NvmeSpecDefs.h>
#include <interfaces/driver/Memory.h>
#include <interfaces/driver/Drivers.h>
#include <Log.h>
#include <Memory.h>
#include <UnitConverter.h>
#include <NanoPrintf.h>

namespace Nvme
{
    bool BeginOp(npk_device_api* api, npk_iop_context* context, npk_iop_frame* iop_frame)
    {
        ASSERT_UNREACHABLE();
    }

    bool EndOp(npk_device_api* api, npk_iop_context* context, npk_iop_frame* iop_frame)
    {
        ASSERT_UNREACHABLE();
    }

    npk_string GetSummary(npk_device_api* api)
    {
        NvmeNamespace* ns = static_cast<NvmeNamespace*>(api->driver_data);
        return npk_string { .length = ns->summaryStr.Size(), .data = ns->summaryStr.Begin() };
    }

    static void DpcShim(void* arg)
    { static_cast<NvmeController*>(arg)->DpcCallback(); }

    bool NvmeController::Enable(bool yes)
    {
        auto& props = *propsVmo->As<volatile ControllerProps>();

        /* When enabling the controller, it's not required to preserve any state in the config field
         * from before it's enabled, so we write everything there that we want in one go.
         * We also leave a number of the fields in this register as zero (like the abitration mode,
         * and selecting the NVM command set).
         */
        if (yes)
        {
            uint32_t config = 1;
            config |= 6 << 16; //IOSQES: IO submission queue entry size, as a power of 2 (64 bytes).
            config |= 4 << 20; //IOCQES: IO completion queue entry size, as a power of 2 (16 bytes).
                               //TODO: set MPS (page size)
            props.config = config;

            while ((props.status & 0b11) == 0)
                sl::HintSpinloop(); //wait for controller to set fatal error bit or enabled bit

            if (props.status & 0b10)
                return false;
            
            Log("NVMe controller enabled, config: 0x%x", LogLevel::Info, props.config);
        }
        else
        {
            props.config = 0;
            while ((props.status & 1) == 1)
                sl::HintSpinloop();

            Log("NVMe controller disabled.", LogLevel::Info);
        }

        return true;
    }

    bool NvmeController::IdentifyController()
    {
        const size_t pageCount = 0x1000 / pageSize; //reponse is 0x1000 bytes
        const uintptr_t pages = npk_pm_alloc_many(pageCount, nullptr);

        SqEntry cmd {};
        cmd.opcode = Ops::AdminIdentify;
        cmd.prp1 = pages; //TODO: we assume this fits in a single page here (it may not) BuildPrps()/CleanupPrps()
        cmd.cdw10 = Cns::Controller;
        VALIDATE_(PollCommand(0, cmd, true), false);

        //there are plenty of fields we ignore in the controller ident response, because the spec
        //covers all needs. Fortunately its well designed so the defaults are fine and most things
        //can be ignored, we dont print a lot of info here, mostly for my personal curiosity.
        sl::NativePtr access(pages + npk_hhdm_base());

        {
            uint8_t serial[20];
            uint8_t model[40];
            uint8_t fwRevision[8];
            sl::memcopy(access.Offset(4).ptr, serial, 20);
            sl::memcopy(access.Offset(24).ptr, model, 40);
            sl::memcopy(access.Offset(64).ptr, fwRevision, 8);
            const size_t serialLen = sl::memfirst(serial, ' ', 20);
            const size_t modelLen = sl::memfirst(model, ' ', 40);
            const size_t fwRevLen = sl::memfirst(fwRevision, ' ', 8);

            Log("Controller id: %.*s %.*s %.*s", LogLevel::Verbose, (int)modelLen, model,
                (int)serialLen, serial, (int)fwRevLen, fwRevision);
        }
        const size_t minPageSize = 0x1000 << ((propsVmo->As<volatile ControllerProps>()->capabilities >> 48) & 0xF);
        maxTransferSize = minPageSize * (1 << access.Offset(77).Read<uint8_t>());
        const uint8_t controllerType = access.Offset(111).Read<uint8_t>();
        VALIDATE_(controllerType == 1, false); //type 1 is IO controller, what we need for this.

        const uint32_t maxNamespaces = access.Offset(516).Read<uint32_t>();
        const uint32_t maxAllowedNamespaces = access.Offset(540).Read<uint32_t>();
        const uint16_t maxInFlightCmds = access.Offset(514).Read<uint16_t>();
        const auto conv = sl::ConvertUnits(maxTransferSize, sl::UnitBase::Binary);
        Log("Maximums: inFlightCmds=%u, namespaces=%u, transferSize=0x%lx (%lu.%lu %sB)", LogLevel::Info,
            maxInFlightCmds, (maxAllowedNamespaces != 0 ? maxAllowedNamespaces : maxNamespaces),
            maxTransferSize, conv.major, conv.minor, conv.prefix);

        npk_pm_free_many(pages, pageCount);
        return true;
    }

    bool NvmeController::IdentifyNamespaces()
    {
        const size_t pageCount = 0x1000 / pageSize; //response is 0x1000 bytes
        const uintptr_t pages = npk_pm_alloc_many(pageCount, nullptr);

        sl::NativePtr access(pages + npk_hhdm_base());
        sl::Vector<uint32_t> nsids;

        bool foundAllNsids = false;
        while (!foundAllNsids)
        {
            SqEntry cmd {};
            cmd.opcode = Ops::AdminIdentify;
            cmd.prp1 = pages; //TODO: we assume this fits in a single page here (it may not)
            cmd.cdw10 = Cns::ActiveNsList;
            cmd.namespaceId = 0; //give us nsids 0 -> 1024
            VALIDATE_(PollCommand(0, cmd, true), false);

            for (size_t i = 0; i < 1024; i++)
            {
                if (access.As<uint32_t>()[i] == 0)
                {
                    foundAllNsids = true;
                    break;
                }
                nsids.PushBack(access.As<uint32_t>()[i]);
            }
        }

        size_t failedIdentifies = 0;
        for (size_t i = 0; i < nsids.Size(); i++)
        {
            SqEntry cmd {};
            cmd.opcode = Ops::AdminIdentify;
            cmd.prp1 = pages;
            cmd.cdw10 = Cns::Namespace;
            cmd.namespaceId = nsids[i];
            if (!PollCommand(0, cmd, true))
            {
                failedIdentifies++;
                Log("Failed get identify data for namespace %u", LogLevel::Error, nsids[i]);
                continue;
            }

            NvmeNamespace& ns = namespaces.EmplaceBack();
            ns.id = nsids[i];
            ns.blockCount = access.Read<uint64_t>();
            const uint8_t formatIndex = [=]()
            {
                const uint8_t source = access.Offset(26).Read<uint8_t>();
                return (source & 0xF) | ((source >> 5) & 3);
            }();

            const uint32_t format = access.Offset(128 + 4 * formatIndex).Read<uint32_t>();
            ns.metadataSize = format & 0xFFFF;
            ns.blockSize = 1 << ((format >> 16) & 0xF);
            //there is also performance info for this format in bits 25:24 (see NVM command set spec, fig 98).

            const auto conv = sl::ConvertUnits(ns.blockCount * ns.blockSize);
            const uint8_t optFeatures = access.Offset(24).Read<uint8_t>();
            Log("NS %u: blockSize=%u, blocks=%lu, capacity=%lu.%lu %sB, metaSize=%u, optFeatures=0x%x",
                LogLevel::Info, ns.id, ns.blockSize, ns.blockCount, conv.major, 
                conv.minor, conv.prefix, ns.metadataSize, optFeatures);
        }
        Log("Identified %lu namespaces, %lu unusable.", LogLevel::Info, nsids.Size(), failedIdentifies);

        access = nullptr;
        npk_pm_free_many(pages, pageCount);
        return true;
    }

    bool NvmeController::CreateAdminQueue(size_t entries)
    {
        if (!queues.Empty())
            return false;

        queuesLock.WriterLock();
        auto& queue = queues.EmplaceBack();
        queuesLock.WriterUnlock();
        sl::ScopedLock queueLock(queue.lock);

        queue.entries = entries;
        const size_t pmAllocSize = npk_pm_alloc_size();
        const size_t cqSize = entries * sizeof(CqEntry);
        const size_t sqSize = entries * sizeof(SqEntry);

        queue.cq = npk_pm_alloc_many(sl::AlignUp(cqSize, pmAllocSize) / pmAllocSize, nullptr);
        if (queue.cq.ptr == nullptr)
            return false;
        queue.sq = npk_pm_alloc_many(sl::AlignUp(sqSize, pmAllocSize) / pmAllocSize, nullptr);
        if (queue.sq.ptr == nullptr)
        {
            npk_pm_free_many(queue.cq.raw, sl::AlignUp(cqSize, pmAllocSize) / pmAllocSize);
            return false;
        }
        const uintptr_t hhdmBase = npk_hhdm_base();
        queue.cq = queue.cq.raw + hhdmBase;
        queue.sq = queue.sq.raw + hhdmBase;
        queue.sqTail = queue.cqHead = 0;
        queue.nextCmdId = 1;
        queue.phase = 1;

        sl::NativePtr doorbellBase = propsVmo->raw + DoorbellBaseOffset;
        queue.sqDoorbell = doorbellBase.As<volatile uint32_t>();
        queue.cqDoorbell = doorbellBase.Offset(doorbellStride).As<volatile uint32_t>();
        if (reinterpret_cast<uintptr_t>(queue.cqDoorbell) >= propsVmo->raw + propsVmo.Size())
        {
            Log("Admin doorbells are out of range", LogLevel::Error);
            npk_pm_free_many(queue.sq.raw, sl::AlignUp(sqSize, pmAllocSize) / pmAllocSize);
            npk_pm_free_many(queue.cq.raw, sl::AlignUp(cqSize, pmAllocSize) / pmAllocSize);
            return false;
        }

        Log("Admin queues created: entries=%lu, cq=0x%lx, sq=0x%lx", LogLevel::Info, entries,
            queue.cq.raw - hhdmBase, queue.sq.raw - hhdmBase);
        return true;
    }

    bool NvmeController::DestroyAdminQueue()
    {
        ASSERT_UNREACHABLE();
    }

    sl::Opt<size_t> NvmeController::CreateIoQueue(size_t entries)
    {
        ASSERT_UNREACHABLE();
    }

    bool NvmeController::DestroyIoQueue(size_t index)
    {
        ASSERT_UNREACHABLE();
    }

    bool NvmeController::PollCommand(size_t queue, SqEntry& cmd, bool updateQueue)
    {
        queuesLock.ReaderLock();
        if (queue >= queues.Size())
        {
            queuesLock.ReaderUnlock();
            return false;
        }
        NvmeQueue& q = queues[queue];
        queuesLock.ReaderUnlock();

        //submit command
        q.lock.Lock();
        cmd.commandId = q.nextCmdId;
        if (q.nextCmdId == 0xFFFF)
            q.nextCmdId = 1; //skip command id 0 so we dont accidentally accept zeroed memory as something valid

        //TODO: queue slot allocator? gives us a command an empty queue slot, so we dont trample ourselves
        void* slot = q.sq.Offset(sizeof(SqEntry) * q.sqTail++).ptr;
        sl::memcopy(&cmd, slot, sizeof(SqEntry));
        q.sqTail = q.sqTail % q.entries;
        *q.sqDoorbell = q.sqTail;

        size_t cqHead = q.cqHead;
        uint8_t phase = q.phase;
        q.lock.Unlock();

        //wait for completion
        while (true)
        {
            auto cq = q.cq.As<volatile CqEntry>();
            uint16_t status = cq[cqHead].status;
            while ((status & 1) == phase)
            {
                if (cq[cqHead].commandId != cmd.commandId)
                {
                    cqHead++;
                    if (cqHead == q.entries)
                    {
                        cqHead = 0;
                        phase = !phase;
                    }
                    status = cq[cqHead].status;
                    continue;
                }

                //this option allows us to use PollCommand while interrupt-based updating of the
                //queue is active, since this function wont interfere with that mechanism.
                if (updateQueue)
                {
                    sl::ScopedLock queueLock(q.lock);
                    if (++cqHead == q.entries)
                    {
                        cqHead = 0;
                        q.phase = !q.phase;
                    }

                    q.cqHead = cqHead;
                    *q.cqDoorbell = q.cqHead;
                }

                //phase and command id match, this is the completion we've been waiting for!
                return (status >> 1) == 0;
            }

            sl::HintSpinloop();
        }
        ASSERT_UNREACHABLE();
    }

    CmdToken NvmeController::BeginCommand(size_t queue, SqEntry& cmd)
    {
        ASSERT_UNREACHABLE();
    }

    bool NvmeController::EndCommand(CmdToken token)
    {
        ASSERT_UNREACHABLE();
    }

    bool NvmeController::Init(const npk_event_add_device* event)
    {
        const npk_init_tag* scan = event->tags;
        while (scan != nullptr)
        {
            if (scan->type == npk_init_tag_type::PciFunction)
            {
                auto pciTag = reinterpret_cast<const npk_init_tag_pci_function*>(scan);
                Log("NVMe controller (over pci) located: %02x::%02x:%02x:%01x", LogLevel::Debug, pciTag->segment,
                    pciTag->bus, pciTag->device, pciTag->function);
                break;
            }
            scan = scan->next;
        }
        VALIDATE_(scan != nullptr, "Unknown transport for NVMe controller, only PCI is supported");
        pciAddr = dl::PciAddress(event->descriptor_id);

        pciAddr.MemorySpaceEnable(true);
        pciAddr.BusMastering(true);
        pciAddr.InterruptDisable(true);

        //For NVMe-over-pci (which is all this driver handles), we can access the controller
        //properties via BAR0.
        const auto bar0 = pciAddr.ReadBar(0);
        propsVmo = dl::VmObject(bar0.length, bar0.base, dl::VmFlag::Mmio | dl::VmFlag::Write);
        VALIDATE_(propsVmo.Valid(), false);

        //disable the controller, and assert some things for fun.
        Enable(false);
        auto& props = *propsVmo->As<volatile ControllerProps>();
        Log("Controller capabilities: 0x%lx", LogLevel::Verbose, props.capabilities);
        VALIDATE((props.capabilities >> 37) & 1, false, "Controller doesn't support NVM command set");

        const size_t nativePageSize = npk_pm_alloc_size();
        const size_t minPageSize = 0x1000 << ((props.capabilities >> 48) & 0xF);
        const size_t maxPageSize = 0x1000 << ((props.capabilities >> 52) & 0xF);
        Log("Controller page sizes: 0x%lx - 0x%lx, native=0x%lx", LogLevel::Verbose, minPageSize, 
            maxPageSize, nativePageSize);
        VALIDATE_(minPageSize <= nativePageSize && maxPageSize >= nativePageSize, false); //TODO: is this asserting necessary?
        pageSize = nativePageSize;

        doorbellStride = 4 << ((props.capabilities >> 32) & 0xF);
        ioQueueMaxEntries = (props.capabilities & 0xFFFF) + 1;
        const uint32_t specVersion = props.version;
        Log("NVMe spec version=%u.%u.%u, doorbellStride=0x%lx, maxQueueEntries=0x%lx",
            LogLevel::Verbose, specVersion >> 16, (specVersion >> 8) & 0xFF, specVersion & 0xFF,
            doorbellStride, ioQueueMaxEntries);

        //before we try to enable the controller we have to create the admin queues
        const size_t adminQueueEntries = nativePageSize / sizeof(SqEntry);
        const uintptr_t hhdmBase = npk_hhdm_base();
        VALIDATE_(CreateAdminQueue(adminQueueEntries), false);
        props.aqa = adminQueueEntries | (adminQueueEntries << 16);
        props.acq = queues[0].cq.raw - hhdmBase;
        props.asq = queues[0].sq.raw - hhdmBase;

        //before enabling the controller, mask all pin-based interrupts.
        props.interruptMaskSet = ~(uint32_t)0;

        //setup interrupts
        dpc.arg = this;
        dpc.function = DpcShim;
        intrRoute.dpc = &dpc;
        intrRoute.callback = nullptr;

        VALIDATE_(npk_add_interrupt_route(&intrRoute, NPK_NO_AFFINITY), false);
        npk_msi_config msiConfig {};
        VALIDATE_(npk_construct_msi(&intrRoute, &msiConfig), false);

        const auto maybeMsi = dl::FindMsi(pciAddr);
        VALIDATE(maybeMsi.HasValue(), false, "Controller must support either MSI or MSI-X");
        msi = *maybeMsi;
        msi.SetVector(0, msiConfig.address, msiConfig.data, true);
        msi.Enable(true);
        Log("Interrupts: %s, addr=0x%lx, data=0x%lx", LogLevel::Verbose, msi.IsMsix() ? "msix" : "msi", 
            msiConfig.address, msiConfig.data);

        Enable(true);

        VALIDATE_(IdentifyController(), false);
        VALIDATE_(IdentifyNamespaces(), false);
        //TODO: create io queues

        for (size_t i = 0; i < namespaces.Size(); i++)
        {
            NvmeNamespace& ns = namespaces[i];

            ns.ioApi.begin_op = BeginOp;
            ns.ioApi.end_op = EndOp;
            ns.ioApi.header.type = npk_device_api_type::Io;
            ns.ioApi.header.get_summary = GetSummary;
            ns.ioApi.header.driver_data = &namespaces[i];
            ns.ioApi.header.id = NPK_INVALID_HANDLE;

            constexpr char FormatStr[] = "ns %u, %lu.%lu %sB";
            const auto conv = sl::ConvertUnits(ns.blockCount * ns.blockSize);
            const size_t summaryLen = npf_snprintf(nullptr, 0, FormatStr, ns.id, 
                conv.major, conv.minor, conv.prefix);
            char* summaryBuff = new char[summaryLen + 1];
            npf_snprintf(summaryBuff, summaryLen + 1, FormatStr, ns.id, conv.major,
                conv.minor, conv.prefix);
            ns.summaryStr = sl::StringSpan(summaryBuff, summaryLen);

            if (!npk_add_device_api(&ns.ioApi.header))
                Log("Failed to register namespace %u with kernel.", LogLevel::Error, ns.id);
            else
                Log("Registered namespace %u as device API %lu", LogLevel::Verbose, ns.id, ns.ioApi.header.id);
        }
        return true;
    }

    bool NvmeController::Deinit()
    {
        ASSERT_UNREACHABLE();
    }

    void NvmeController::DpcCallback()
    {
        Log("Nvme DPC", LogLevel::Debug);
    }
}
