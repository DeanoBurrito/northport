#include <devices/pci/NvmeController.h>
#include <memory/PhysicalMemory.h>
#include <devices/PciBridge.h>
#include <InterruptManager.h>
#include <Log.h>

namespace Kernel::Devices::Pci
{
    namespace Ops
    {
        //These are taken from the NVMe base specification + NVM command set spec.
        constexpr uint8_t DeleteIoSQ = 0x0;
        constexpr uint8_t CreateIoSQ = 0x1;
        constexpr uint8_t GetLogPage = 0x2;
        constexpr uint8_t DeleteIoCQ = 0x4;
        constexpr uint8_t CreateIoCQ = 0x5;
        constexpr uint8_t Identify = 0x6;
        constexpr uint8_t Abort = 0x8;
        constexpr uint8_t SetFeatures = 0x9;
        constexpr uint8_t GetFeatures = 0xA;
        constexpr uint8_t FirmwareDownload = 0x11;
        constexpr uint8_t SelfTest = 0x14;
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

    void NvmeInterruptStub(size_t vector, void* arg)
    {
        NvmeController* driver = static_cast<NvmeController*>(arg);
        driver->HandleEvent(Drivers::DriverEventType::Interrupt, (void*)vector);
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
            for (size_t i = 0; i < msix->Vectors(); i++)
            {
                msix->SetVector(i, msiAddr.raw, msiMessage, address);
                msix->Mask(i, false, address);
            }
        }

        InterruptManager::Global()->AttachCallback(*maybeIntVector, NvmeInterruptStub, this);
    }
    
    void NvmeController::CreateAdminQueue(size_t queueEntries)
    {
        queues.EnsureCapacity(1);
        
        queueEntries = sl::min(queueEntries, maxQueueEntries);
        sl::NativePtr doorbellBase = (uintptr_t)properties + 0x1000;
        const size_t dbShift = 4 << doorbellStride;
        
        NvmeQueue& q = queues[0]; //peak variable naming
        q.entries = queueEntries;
        q.cqHead = 0;
        q.sqTail = 0;
        q.cqPhase = 1;

        const size_t sqSizeBytes = queueEntries * sizeof(SubmissionQueueEntry);
        q.submission = Memory::PMM::Global()->AllocPages(sqSizeBytes / PAGE_FRAME_SIZE + 1);
        q.submission = EnsureHigherHalfAddr(q.submission.ptr);
        q.submissionTail = doorbellBase.As<volatile uint32_t>();

        const size_t cqSizeBytes = queueEntries * sizeof(CompletionQueueEntry);
        q.completion = Memory::PMM::Global()->AllocPages(cqSizeBytes / PAGE_FRAME_SIZE + 1);
        q.completion = EnsureHigherHalfAddr(q.completion.ptr);
        q.completionHead = doorbellBase.As<volatile uint32_t>(4);
    }

    void NvmeController::DestroyAdminQueue()
    { /* TODO: */ }

    void NvmeController::CreateIoQueue(size_t index)
    {

    }

    void NvmeController::DestroyIoQueue(size_t index)
    { /* TODO: */ }

    NvmeCmdResult NvmeController::DoCommand(size_t queueIndex, SubmissionQueueEntry* cmd)
    {
        NvmeQueue& queue = queues[queueIndex];
        cmd->fields.commandId = queue.nextCommandId++;

        volatile SubmissionQueueEntry* sqTail = queue.submission.As<SubmissionQueueEntry>();
        sqTail[queue.sqTail] = *cmd;
        queue.sqTail++;
        if (queue.sqTail == queue.entries)
            queue.sqTail = 0;
        *queue.submissionTail = queue.sqTail;

        volatile CompletionQueueEntry* cqHead = queue.completion.As<volatile CompletionQueueEntry>();
        cqHead += queue.cqHead;
        
        while ((cqHead->fields.status & 1) != queue.cqPhase);

        //bit 0 is the phase bit, which is irrelevent to checking the completion status
        const uint32_t result = cqHead->fields.status >> 1;
        if (result != 0)
            Logf("NVMe synchronous command failed: opcode=0x%x, error=0x%x", LogSeverity::Error, cmd->fields.opcode, result);
        
        if (queue.cqHead + 1 == queue.entries)
        {
            queue.cqPhase = !queue.cqPhase;
            queue.cqHead = 0;
        }
        else
            queue.cqHead++;
        
        *queue.completionHead = queue.cqHead;
        return result;
    }

    sl::BufferView NvmeController::Identify(IdentifyCns cns, uint32_t namespaceId, sl::BufferView buffer)
    {
        if (buffer.base.ptr == nullptr || buffer.length != PAGE_FRAME_SIZE)
            return {};
        
        SubmissionQueueEntry submission;
        submission.fields.opcode = Ops::Identify;
        submission.fields.prp1 = EnsureLowerHalfAddr(buffer.base.raw);
        submission.fields.prp2 = 0;
        submission.fields.namespaceId = namespaceId;
        submission.fields.cdw10 = (uint8_t)cns;

        NvmeCmdResult result = DoCommand(0, &submission);
        if (result != 0)
            return {};

        return { EnsureHigherHalfAddr(submission.fields.prp1), 0x1000 };
    }

    void NvmeController::HandleInterrupt(size_t vector)
    {
        Log("NVMe controller got interrupt!", LogSeverity::Debug);
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

        uint32_t command = address.ReadReg(PciRegCmdStatus);
        command |= 1 << 1; //memory space access
        command |= 1 << 2; //bus mastering
        command |= 1 << 10; //disable the pin interrupts
        address.WriteReg(PciRegCmdStatus, command);
        
        //ensure mmio described by BAR0 is mapped properly
        const PciBar bar0 = address.ReadBar(0);
        //TODO: check if mmio area is mapped within hhdm. Maybe some kind of MapMmio() function?
        //We could also do something like PciAddress.EnsureMapped() and have it allocate a virtual range to access the bar that way.

        properties = reinterpret_cast<volatile NvmePropertyMap*>(bar0.address);
        properties = EnsureHigherHalfAddr(properties);
        
        //disable the controller while we mess with it's settings.
        properties->controllerConfig &= ~(uint32_t)1; //clear the enable bit
        while ((properties->controllerStatus & 1) != 0); //wait until driver clears the ready bit

        doorbellStride = (properties->capabilities >> 32) & 0b1111;
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
        CreateAdminQueue(16); //admin queue is always 0.
        properties->adminQueueAttribs = (maxQueueEntries << 16) | maxQueueEntries;
        properties->adminCompletionQueue = EnsureLowerHalfAddr(queues[0].completion.raw & ~0xFFFul);
        properties->adminSubmissionQueue = EnsureLowerHalfAddr(queues[0].submission.raw & ~0xFFFul);

        //controllers are not expected to maintain the previous state of this register when bit 0 (enable)
        //is set, so we have to write all the values we want at once.
        uint32_t cc = 1; //enable bit
        cc |= 0 << 4;  //SHN: use the NVM command set
        cc |= 0 << 11; //AMS: round-robin queue arbitration
        cc |= 6 << 16; //IOSQES: submission queue entry size, as a power of 2. 6^2 = 64 bytes.
        cc |= 4 << 20; //IOCSES: completion queue entry size, like above. 4^2 = 16 bytes
        cc |= 0 << 7;  //MPS: (this + 12) ^ 2 is the base page size we supported. 4K is always supported, so we stick with that.
        //CSS (current selected [command] set) is also set to zero here, meaning use the NVM command set.
        properties->controllerConfig = cc;

        //wait for controller to come online
        while ((properties->controllerStatus & 1) == 0); //TODO: we should also check for an error state here
        
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
        sl::BufferView namespaceList = Identify(IdentifyCns::ActiveNamespaces, 0, identifyBuffer);
        const uint32_t* list = namespaceList.base.As<uint32_t>();
        //max of 1024 ids per identify list, id=0 means an id is invalid.
        for (size_t i = 0; list[i] != 0 && i < 1024; i++)
            namespaces.EmplaceBack(list[i]);
        
        for (size_t i = 0; i < namespaces.Size(); i++)
        {
            sl::BufferView nsInfo = Identify(IdentifyCns::Nsid, namespaces[i].nsid, identifyBuffer);
            //LBA formats (lbaf) begin at byte 128. Bits 23:16 of an LBAF are the lba size as a power of 2.
            //we're only going to use format 1, as the other formats are optionally and we dont need them right now.
            namespaces[i].blockCount = *nsInfo.base.As<uint8_t>(0);
            namespaces[i].blockSize = 1 << *nsInfo.base.As<uint8_t>(130);
            Logf("NVMe namspace found: id=%u, blocks=%u, blockSize=%U", LogSeverity::Verbose, 
                i, namespaces[i].blockCount, namespaces[i].blockSize);
        }

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

        //create IO queues, at least one queue is always supported.
        //To do this properly we would use GetFeatures to check the maximum number of io queues,
        //and then allocate as many as we wanted up to that limit.
        CreateIoQueue(1);
    }

    void NvmeController::Deinit()
    { /* TODO: */ }

    void NvmeController::HandleEvent(Drivers::DriverEventType type, void* args)
    {}

    Drivers::GenericDriver* CreateNewNvmeDriver()
    { 
        return new NvmeController();
    }
}
