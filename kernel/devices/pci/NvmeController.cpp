#include <devices/pci/NvmeController.h>
#include <devices/PciBridge.h>
#include <devices/DeviceManager.h>
#include <memory/PhysicalMemory.h>
#include <InterruptManager.h>
#include <Locks.h>
#include <Log.h>

namespace Kernel::Devices::Pci
{
    //These are taken from the NVMe base specification + NVM command set spec.
    namespace Ops
    {
        namespace Admin
        {
            constexpr uint8_t CreateIoSQ = 0x1;
            constexpr uint8_t CreateIoCQ = 0x5;
            constexpr uint8_t Identify = 0x6;
            constexpr uint8_t GetFeatures = 0xA;
        }

        namespace IO
        {
            // constexpr uint8_t Flush = 0x0;
            constexpr uint8_t Write = 0x1;
            constexpr uint8_t Read = 0x2;
            // constexpr uint8_t WriteZeroes = 0x8;
        }
    }

    SubmissionQueueEntry::SubmissionQueueEntry()
    {
        for (size_t i = 0; i < 16; i++)
            dwords[i] = 0;
    }

    volatile SubmissionQueueEntry& SubmissionQueueEntry::operator=(const SubmissionQueueEntry& other) volatile
    {
        if (this == &other)
            return *this;
        
        for (size_t i = 0; i < 16; i++)
            this->dwords[i] = other.dwords[i];
        return *this;
    }

    CompletionQueueEntry& CompletionQueueEntry::operator=(const volatile CompletionQueueEntry& other)
    {
        if (this == &other)
            return *this;
        
        for (size_t i = 0; i < 4; i++)
            this->dwords[i] = other.dwords[i];
        return *this;
    }

    void NvmeInterruptStub(size_t vector, void* arg)
    {
        NvmeController* driver = static_cast<NvmeController*>(arg);
        driver->HandleEvent(Drivers::DriverEventType::Interrupt, (void*)vector);
    }

    void NvmeController::Disable()
    {
        //clear the enable bit and wait for the ready bit to cleared (indicating its disabled)
        properties->controllerConfig &= ~(uint32_t)1;
        while ((properties->controllerStatus & 1) != 0);
    }

    bool NvmeController::Enable()
    {
        //The controller doesn't need to maintain the previous state once we set the enable bit,
        //so we have to write our settings in a single write.

        //There are number of fields we initialize to zero as well:
        //AMS: abitration scheme. 0 = round robin.
        //MPS: page size to use, encoded as (12 + n) ^ 2. So 0 means 12 ^ 2 = 4096 bytes.
        //CSS: current command set: 0 = NVM command set. There are others, but this is what we care about.

        uint32_t cc = 1; //enable bit
        cc |= 6 << 16; //IOSQES: submission queue entry size, as a power of 2. 6 ^ 2 = 64 bytes.
        cc |= 4 << 20; //IOCSES: completion queue entry size, as apower of 2. 4 ^ 2 = 16 bytes.
        properties->controllerConfig = cc;

        while ((properties->controllerStatus & 0b11) == 0); //bit 0 is enable bit

        return (properties->controllerStatus & 0b10) == 0; //bit 1 is fatal error status
    }

    void NvmeController::InitInterrupts(sl::Opt<PciCap*>& maybeMsi, sl::Opt<PciCap*>& maybeMsiX)
    {
        //we're going to route everything to a single interrupt vector.
        auto maybeIntVector = InterruptManager::Global()->AllocVectors(1);
        if (!maybeIntVector)
        {
            Log("Could not allocate interrupt for NVMe MSI, aborting init.", LogSeverity::Error);
            return;
        }

        interruptVector = *maybeIntVector;

        const sl::NativePtr msiAddr = InterruptManager::GetMsiAddr(0);
        const uint64_t msiMessage = InterruptManager::GetMsiData(*maybeIntVector);
        if (maybeMsi)
        {
            PciCapMsi* msi = static_cast<PciCapMsi*>(*maybeMsi);
            this->msi = msi;

            msi->Enable(true);
            msi->Mask(0, false);
            msi->SetVectorsEnabled(1);
            msi->SetAddress(msiAddr);
            msi->SetData(msiMessage);

        }
        else if (maybeMsiX)
        {
            PciCapMsiX* msix = static_cast<PciCapMsiX*>(*maybeMsiX);
            this->msi = msix;
            
            msix->Enable(true);
            msix->MaskFunctions(false);
            //leave msix vector 0 masked (admin queue, we poll that), and unmask all others (ioqueues).
            for (size_t i = 0; i < msix->Vectors(); i++)
                msix->SetVector(i, msiAddr.raw, msiMessage, i == 0, address);
        }

        InterruptManager::Global()->AttachCallback(*maybeIntVector, NvmeInterruptStub, this);
    }
    
    void NvmeController::CreateAdminQueue(size_t queueEntries)
    {
        queues.EnsureCapacity(1);
        while (queues.Size() < 1)
            queues.EmplaceBack();
        
        queueEntries = sl::min(queueEntries, maxQueueEntries);
        sl::NativePtr doorbellBase = (uintptr_t)properties + 0x1000;
        
        NvmeQueue& q = queues[0]; //peak variable naming
        sl::ScopedSpinlock qLock(&q.lock);

        q.entries = queueEntries;
        q.cqHead = 0;
        q.sqTail = 0;
        q.cqPhase = 1;
        q.nextCommandId = 1;

        const size_t sqSizeBytes = queueEntries * sizeof(SubmissionQueueEntry);
        q.submission = Memory::PMM::Global()->AllocPages(sqSizeBytes / PAGE_FRAME_SIZE + 1);
        q.submission = EnsureHigherHalfAddr(q.submission.ptr);
        q.sqDoorbell = doorbellBase.As<volatile uint32_t>();

        const size_t cqSizeBytes = queueEntries * sizeof(CompletionQueueEntry);
        q.completion = Memory::PMM::Global()->AllocPages(cqSizeBytes / PAGE_FRAME_SIZE + 1);
        q.completion = EnsureHigherHalfAddr(q.completion.ptr);
        q.cqDoorbell = doorbellBase.As<volatile uint32_t>(doorbellStride);
    }

    void NvmeController::DestroyAdminQueue()
    { 
        if (queues.Size() != 1)
        {
            Log("Cannot clenaup NVMe admin queue: IO queues still exist.", LogSeverity::Error);
            return;
        }

        Disable();
        Log("NVMe controller disabled, cleaning up admin queues.", LogSeverity::Verbose);

        NvmeQueue aq = queues.PopBack();
        const size_t sqPages = aq.entries * sizeof(SubmissionQueueEntry) / PAGE_FRAME_SIZE + 1;
        const size_t cqPages = aq.entries * sizeof(CompletionQueueEntry) / PAGE_FRAME_SIZE + 1;
        Memory::PMM::Global()->FreePages(EnsureLowerHalfAddr(aq.submission.ptr), sqPages);
        Memory::PMM::Global()->FreePages(EnsureLowerHalfAddr(aq.completion.ptr), cqPages);

        properties->adminCompletionQueue = 0;
        properties->adminSubmissionQueue = 0;
        properties->adminQueueAttribs = 0;
    }

    void NvmeController::CreateIoQueue(size_t index)
    {
        queues.EnsureCapacity(index + 1);
        while (queues.Size() <= index)
            queues.EmplaceBack();
        
        const size_t sqPages = maxQueueEntries * sizeof(SubmissionQueueEntry) / PAGE_FRAME_SIZE + 1;
        const size_t cqPages = maxQueueEntries * sizeof(CompletionQueueEntry) / PAGE_FRAME_SIZE + 1;
        NvmeQueue& queue = queues[index];
        sl::ScopedSpinlock scopeLock(&queue.lock);

        queue.entries = maxQueueEntries;
        queue.cqHead = queue.sqTail = 0;
        queue.cqPhase = 1;
        queue.nextCommandId = 1;

        sl::NativePtr doorbellBase = (uintptr_t)properties + 0x1000 + (2 * index) * doorbellStride;

        queue.submission = EnsureHigherHalfAddr(Memory::PMM::Global()->AllocPages(sqPages));
        queue.sqDoorbell = doorbellBase.As<volatile uint32_t>();
        
        queue.completion = EnsureHigherHalfAddr(Memory::PMM::Global()->AllocPages(cqPages));
        queue.cqDoorbell = doorbellBase.As<volatile uint32_t>(doorbellStride);

        //now we need to tell the controller about the buffer we've created for the queues.
        SubmissionQueueEntry createCqCmd;
        createCqCmd.fields.opcode = Ops::Admin::CreateIoCQ;
        createCqCmd.fields.prp1 = EnsureLowerHalfAddr(queue.completion.raw);
        createCqCmd.fields.cdw10 = (maxQueueEntries - 1) << 16 | index;
        createCqCmd.fields.cdw11 = 1 << 16 | 0b11; //use msix vector 0, enable interrupts and PC bit.
        (void)DoAdminCommand(&createCqCmd);

        SubmissionQueueEntry createSqCmd;
        createSqCmd.fields.opcode = Ops::Admin::CreateIoSQ;
        createSqCmd.fields.prp1 = EnsureLowerHalfAddr(queue.submission.raw);
        createSqCmd.fields.cdw10 = (maxQueueEntries - 1) << 16 | index;
        //bit 0 (PC - Physical contiguous) is set, the other options being a scatter-gather setup.
        createSqCmd.fields.cdw11 = index << 16 | 1; 
        (void)DoAdminCommand(&createSqCmd);

        Logf("NVMe IO queue pair created: id=%u, entries=%u, sqBase=0x%lx, cqBase=0x%lx", LogSeverity::Verbose, 
            index, maxQueueEntries, EnsureLowerHalfAddr(queue.submission.raw), EnsureLowerHalfAddr(queue.completion.raw));
    }

    void NvmeController::DestroyIoQueue(size_t index)
    { /* TODO: */ }

    NvmeCmdResult NvmeController::DoAdminCommand(SubmissionQueueEntry* cmd, CompletionQueueEntry* completion)
    {
        NvmeQueue& queue = queues[0];
        sl::ScopedSpinlock qLock(&queue.lock);
        cmd->fields.commandId = queue.nextCommandId++;

        volatile SubmissionQueueEntry* sqTail = queue.submission.As<SubmissionQueueEntry>();
        sqTail[queue.sqTail] = *cmd;
        queue.sqTail++;
        if (queue.sqTail == queue.entries)
            queue.sqTail = 0;
        *queue.sqDoorbell = queue.sqTail;

        volatile CompletionQueueEntry* cqHead = queue.completion.As<volatile CompletionQueueEntry>();
        cqHead += queue.cqHead;
        
        while ((cqHead->fields.status & 1) != queue.cqPhase);

        //bit 0 is the phase bit, which is irrelevent to checking the completion status
        const uint32_t result = cqHead->fields.status >> 1;
        if (result != 0)
        {
            Logf("NVMe synchronous command failed: opcode=0x%x, type=0x%x, statusCode=0x%x, dnr=%b", LogSeverity::Error, 
                cmd->fields.opcode, (result >> 8) & 0b111, result & 0xFF, result & (1 << 14));
        }

        if (completion != nullptr)
            *completion = *cqHead;
        
        if (queue.cqHead + 1 == queue.entries)
        {
            queue.cqPhase = !queue.cqPhase;
            queue.cqHead = 0;
        }
        else
            queue.cqHead++;
        
        *queue.cqDoorbell = queue.cqHead;
        return result;
    }

    bool NvmeController::BuildPrps(NvmeQueue& queue, SubmissionQueueEntry& cmd, sl::BufferView buffer)
    {
        //create a prplist and populate prp1 & 2 appropriately
        const size_t prps = buffer.length / PAGE_FRAME_SIZE;
        if (prps > 0x1000)
            return false;

        if (prps == 1)
        {
            cmd.fields.prp1 = EnsureLowerHalfAddr(buffer.base.raw);
            return true;
        }
        else if (prps == 2)
        {
            cmd.fields.prp1 = EnsureLowerHalfAddr(buffer.base.raw);
            cmd.fields.prp2 = EnsureLowerHalfAddr(buffer.base.raw + PAGE_FRAME_SIZE);
            return true;
        }

        //the buffer is too big to fit into 2 pointers, we'll need to create a prp list.
        //we alloc a new phys page for prp2 to hold all the overflow pages.
        sl::NativePtr prplPage = EnsureHigherHalfAddr(Memory::PMM::Global()->AllocPage());
        queue.prplists.PushBack({ cmd.fields.commandId, EnsureLowerHalfAddr(prplPage.ptr) });
        volatile uint64_t* prplAccess = prplPage.As<volatile uint64_t>();

        NativeUInt prp = EnsureLowerHalfAddr(buffer.base.raw);
        cmd.fields.prp1 = prp;
        cmd.fields.prp2 = EnsureLowerHalfAddr(prplPage.raw);

        for (size_t i = 1; i < prps; i++)
            prplAccess[i - 1] = prp + i * PAGE_FRAME_SIZE;
        return true;
    }

    size_t NvmeController::BeginIoCmd(size_t nsid, size_t lbaStart, sl::BufferView buffer, uint8_t opcode)
    {
        if (!GetHhdm().Contains(buffer))
        {
            Log("NVMe driver failed to start IO operation: provided buffer not in hhdm.", LogSeverity::Error);
            return (size_t)-1;
        }

        const NvmeNamespace* ns = nullptr;
        for (size_t i = 0; i < namespaces.Size(); i++)
        {
            if (namespaces[i].nsid == nsid)
            {
                ns = &namespaces[i];
                break;
            }
        }
        if (ns == nullptr)
        {
            Log("NVMe driver failed to start IO operation: invalid namespace id.", LogSeverity::Error);
            return (size_t)-1;
        }

        if (buffer.length % ns->blockSize != 0)
        {
            Log("NVMe driver failed to start IO operation: data length not LBA-aligned.", LogSeverity::Error);
            return (size_t)-1;
        }

        //TODO: check we're not reading/writing past the end of the namespace

        NvmeQueue& ioQueue = queues[1]; //TODO: avoid hardcoding of queues like this
        sl::ScopedSpinlock queueLock(&ioQueue.lock);

        SubmissionQueueEntry cmd;
        cmd.fields.opcode = opcode;
        cmd.fields.cdw10 = lbaStart;
        cmd.fields.cdw11 = lbaStart >> 32;
        cmd.fields.cdw12 = buffer.length / ns->blockSize - 1;
        cmd.fields.namespaceId = nsid;

        if (!BuildPrps(ioQueue, cmd, buffer))
        {
            Log("NVMe driver failed to start IO operation: Could not build PRP list.", LogSeverity::Error);
            return (size_t)-1;
        }

        const size_t opId = cmd.fields.commandId = ioQueue.nextCommandId;
        ioQueue.nextCommandId++;

        volatile SubmissionQueueEntry* subQueue = ioQueue.submission.As<volatile SubmissionQueueEntry>();
        subQueue[ioQueue.sqTail] = cmd;
        ioQueue.sqTail++;
        if (ioQueue.sqTail == ioQueue.entries)
            ioQueue.sqTail = 0;
        *ioQueue.sqDoorbell = ioQueue.sqTail;
        
        return opId;
    }

    sl::Opt<NvmeCmdResult> NvmeController::EndIoCmd(size_t operationId)
    {
        NvmeQueue& ioQueue = queues[1];
        sl::ScopedSpinlock queueLock(&ioQueue.lock);

        volatile CompletionQueueEntry* comQueue = ioQueue.completion.As<volatile CompletionQueueEntry>();
        
        //search backwards from the last-known completion, if we reach older command ids we've gone too far.
        //TODO: better way to detect pending commands, this is really naive.
        size_t scanHead = ioQueue.cqHead;
        while (comQueue[scanHead].fields.commandId > operationId)
            scanHead--;
        if (comQueue[scanHead].fields.commandId != operationId)
            return {};
        
        const NvmeCmdResult result = comQueue[scanHead].fields.status >> 1;
        if (result != 0)
        {
            Logf("NVMe async operation failed: commandId=0x%x, type=0x%x, statusCode=0x%x, dnr=%b", LogSeverity::Error, 
                operationId, (result >> 8) & 0b111, result & 0xFF, result & (1 << 14));
        }
        
        //if we allocated an extra page for a prplist, we can free it now.
        for (size_t i = 0; i < ioQueue.prplists.Size(); i++)
        {
            if (ioQueue.prplists[i].commandId != operationId)
                continue;
            
            Memory::PMM::Global()->FreePage(ioQueue.prplists[i].buffer.ptr);
            ioQueue.prplists.Erase(i);
            break;
        }
        
        return result;
    }

    sl::BufferView NvmeController::Identify(IdentifyCns cns, uint32_t namespaceId, sl::BufferView buffer)
    {
        if (buffer.base.ptr == nullptr || buffer.length != PAGE_FRAME_SIZE)
            return {};
        
        SubmissionQueueEntry cmd;
        cmd.fields.opcode = Ops::Admin::Identify;
        cmd.fields.prp1 = EnsureLowerHalfAddr(buffer.base.raw);
        cmd.fields.namespaceId = namespaceId;
        cmd.fields.cdw10 = (uint8_t)cns;

        NvmeCmdResult result = DoAdminCommand(&cmd);
        if (result != 0)
            return {};

        return { EnsureHigherHalfAddr(cmd.fields.prp1), 0x1000 };
    }

    size_t NvmeController::GetMaxIoQueueCount()
    {
        SubmissionQueueEntry cmd;
        cmd.fields.opcode = Ops::Admin::GetFeatures;
        cmd.fields.cdw10 = (uint8_t)FeatureId::NumberOfQueues | ((uint8_t)FeatureAttrib::Supported << 8);

        CompletionQueueEntry completion;
        NvmeCmdResult result = DoAdminCommand(&cmd, &completion);
        if (result != 0)
            return 0;
        return completion.dwords[0] & 0xFFFF;
    }

    void NvmeController::Init(Drivers::DriverInitInfo* initInfo)
    {
        if (!PciBridge::Global()->EcamAvailable())
        {
            //I actually dont think it's possible to reach this situation, but it's nice to check.
            Log("Cannot initialize NVMe controller, PCIe ECAM is required.", LogSeverity::Error);
            return;
        }

        auto maybeAddrTag = initInfo->FindTag(Drivers::DriverInitTagType::PciFunction);
        address = reinterpret_cast<Drivers::DriverInitTagPci*>(*maybeAddrTag)->address;

        address.EnableMemoryAddressing();
        address.EnableBusMastering();
        address.EnablePinInterrupts(false);
        address.EnsureBarMapped(0);
        
        //for NVMe over PCI, BAR0 is the address of the properties table.
        properties = reinterpret_cast<volatile NvmePropertyMap*>(address.ReadBar(0).address);
        properties = EnsureHigherHalfAddr(properties);
        
        //controller must be disabled while we set up the admin queues.
        Disable();

        doorbellStride = 4 << ((properties->capabilities >> 32) & 0b1111);
        maxQueueEntries = (properties->capabilities & 0xFFFF) + 1;

        //check the device supports the NVM command set, which we require.
        if ((properties->capabilities >> 37 & 1) == 0)
        {
            Log("NVMe controller does not support NVM command set, aborting driver init.", LogSeverity::Error);
            return;
        }

        const size_t majorVer = properties->version >> 16;
        const size_t minorVer = (properties->version >> 8) & 0xFF;
        const size_t tertiaryVer = properties->version & 0xFF;
        Logf("Attempting to initialize NVMe controller: maxQueueEntries=%u, doorbellStride=%u, nvmeVersion=%u.%u.%u", LogSeverity::Verbose, 
            maxQueueEntries, doorbellStride, majorVer, minorVer, tertiaryVer);
        
        //create admin queue, and tell controller about it.
        CreateAdminQueue(16);
        properties->adminQueueAttribs = (maxQueueEntries << 16) | maxQueueEntries;
        properties->adminCompletionQueue = EnsureLowerHalfAddr(queues[0].completion.raw & ~0xFFFul);
        properties->adminSubmissionQueue = EnsureLowerHalfAddr(queues[0].submission.raw & ~0xFFFul);

        if (!Enable())
        {
            Log("NVMe failed to reinitialize, reporting a fatal error.", LogSeverity::Error);
            return;
        }
        
        const size_t minPageShift = (12 + ((properties->capabilities >> 48) & 0xF));
        if ((1 << minPageShift) != PAGE_FRAME_SIZE)
            Logf("NVMe controller reports awkward minimum page size: reported=%U, physical=%U", LogSeverity::Warning, 1 << minPageShift, PAGE_FRAME_SIZE);

        sl::BufferView identifyBuffer = { Memory::PMM::Global()->AllocPage(), PAGE_FRAME_SIZE };

        //identify controller
        sl::BufferView controllerData = Identify(IdentifyCns::Controller, 0, identifyBuffer);
        const uint8_t encodedMaxTransferSize = *controllerData.base.As<uint8_t>(77);
        if (encodedMaxTransferSize > 0)
            maxTransferSize = 1 << (minPageShift + encodedMaxTransferSize);
        else
            maxTransferSize = 1 << 20; //technically should be infinite, but this is effectively infinite for our purposes.
        
        Logf("NVMe identify response: transferLimit=%U, serial=%#.20s, model=%#.40s", LogSeverity::Verbose, 
            maxTransferSize, controllerData.base.As<void>(4), controllerData.base.As<void>(24));

        //get the active namespace list
        sl::BufferView namespaceList = Identify(IdentifyCns::ActiveNamespacesList, 0, identifyBuffer);
        const uint32_t* list = namespaceList.base.As<uint32_t>();
        //max of 1024 ids per identify list, id=0 means an id is invalid.
        for (size_t i = 0; list[i] != 0 && i < 1024; i++)
            namespaces.EmplaceBack(list[i]);
        
        for (size_t i = 0; i < namespaces.Size(); i++)
        {
            sl::BufferView nsInfo = Identify(IdentifyCns::Namespace, namespaces[i].nsid, identifyBuffer);
            //LBA formats (lbaf) begin at byte 128. Bits 23:16 of an LBAF are the lba size as a power of 2.
            //we're only going to use format 1, as the other formats are optional and we dont need them right now.
            namespaces[i].blockCount = *nsInfo.base.As<uint8_t>(0);
            namespaces[i].blockSize = 1 << *nsInfo.base.As<uint8_t>(130);
            Logf("NVMe namspace found: id=%u, blocks=%u, blockSize=%U", LogSeverity::Verbose, 
                namespaces[i].nsid, namespaces[i].blockCount, namespaces[i].blockSize);

            namespaces[i].blockDevice = new NvmeBlockDevice(this, namespaces[i].nsid);
        }
        Memory::PMM::Global()->FreePage(identifyBuffer.base.ptr);
        identifyBuffer.base = nullptr;

        //I'm refusing to use the interrupt pins, so check that we have an MSI or MSI-X cap.
        auto maybeMsi = FindPciCap(address, CapIdMsi);
        auto maybeMsiX = FindPciCap(address, CapIdMsiX);
        if (!maybeMsi && !maybeMsiX)
        {
            //I'll be impressed if this statement ever gets printed.
            Log("NVMe controller does not support MSI or MSI-X, aborting init.", LogSeverity::Error);
            return;
        }
        InitInterrupts(maybeMsi, maybeMsiX);

        //ideally you would create more than a single queue, but its all we need for now.
        const size_t maxQueueCount = GetMaxIoQueueCount();
        CreateIoQueue(1);

        //register the namespaces as block devices
        for (size_t i = 0; i < namespaces.Size(); i++)
            DeviceManager::Global()->RegisterDevice(namespaces[i].blockDevice);
    }

    void NvmeController::Deinit()
    { 
        //remove any block devices we created
        for (size_t i = 0; i < namespaces.Size(); i++)
        {
            delete DeviceManager::Global()->UnregisterDevice(namespaces[i].blockDevice->GetId());
            namespaces[i].blockCount = namespaces[i].blockSize = namespaces[i].nsid = 0;
        }
        namespaces.Clear();
        
        //destroy io queues, and then admin queue
        while (queues.Size() > 1)
            DestroyIoQueue(queues.Size() - 1);
        DestroyAdminQueue();
        
        //cleanup MSI, free vector
        size_t intVector = 0;
        if (msi->capabilityId == CapIdMsi)
            static_cast<PciCapMsi*>(msi)->Enable(false);
        else
            static_cast<PciCapMsiX*>(msi)->Enable(false);
        InterruptManager::Global()->DetachCallback(interruptVector);
        InterruptManager::Global()->FreeVectors(interruptVector, 1);
    }

    void NvmeController::HandleEvent(Drivers::DriverEventType type, void*)
    {
        if (type != Drivers::DriverEventType::Interrupt)
        {
            Log("NVMe driver received unexpected event type.", LogSeverity::Error);
            return;
        }

        NvmeQueue& queue = queues[1];
        if (queue.cqHead + 1 == queue.entries)
        {
            queue.cqPhase = !queue.cqPhase;
            queue.cqHead = 0;
        }
        else
            queue.cqHead++;
        
        //update completion queue doorbell
        //This is probably the wrong place to update the doorbell, as the controller may overwrite
        //previous commands if we submit enough, resulting in dropped completion entries.
        *queue.cqDoorbell = queue.cqHead;
    }

    size_t NvmeController::BeginRead(size_t nsid, size_t lbaStart, sl::BufferView dest)
    {
        return BeginIoCmd(nsid, lbaStart, dest, Ops::IO::Read);
    }

    sl::Opt<NvmeCmdResult> NvmeController::EndRead(size_t operationId)
    {
        return EndIoCmd(operationId);
    }

    size_t NvmeController::BeginWrite(size_t nsid, size_t lbaStart, sl::BufferView source)
    {
        return BeginIoCmd(nsid, lbaStart, source, Ops::IO::Write);
    }

    sl::Opt<NvmeCmdResult> NvmeController::EndWrite(size_t operationId)
    {
        return EndIoCmd(operationId);
    }

    Drivers::GenericDriver* CreateNewNvmeDriver()
    { 
        return new NvmeController();
    }

    void NvmeBlockDevice::Init()
    {
        sl::SpinlockRelease(&lock);
        type = DeviceType::Block;
        state = DeviceState::Ready;
    }

    void NvmeBlockDevice::Deinit()
    {
        state = DeviceState::Shutdown;
    }

    void NvmeBlockDevice::Reset()
    {
        Deinit();
        Init();
    }

    sl::Opt<Drivers::GenericDriver*> NvmeBlockDevice::GetDriverInstance()
    { 
        return driver;
    }

    size_t NvmeBlockDevice::BeginRead(size_t startLba, IoBlockBuffer& dest)
    {
        return driver->BeginRead(nsid, startLba, dest.memory);
    }

    sl::Opt<BlockCmdResult> NvmeBlockDevice::EndRead(size_t token)
    {
        return driver->EndRead(token);
    }

    size_t NvmeBlockDevice::BeginWrite(size_t startLba, IoBlockBuffer& source)
    {
        return driver->BeginWrite(nsid, startLba, source.memory);
    }

    sl::Opt<BlockCmdResult> NvmeBlockDevice::EndWrite(size_t token)
    {
        return driver->EndWrite(token);
    }
}
