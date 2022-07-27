#pragma once

#include <devices/pci/NvmeDefs.h>
#include <devices/pci/PciAddress.h>
#include <devices/pci/PciCapabilities.h>
#include <devices/interfaces/GenericBlock.h>
#include <drivers/GenericDriver.h>
#include <containers/Vector.h>
#include <BufferView.h>

namespace Kernel::Devices::Pci
{
    /*
        NVMe has a lot of moving pieces to get setup, and a lot of new terminology. The key parts are getting
        the controller set up, scanning for namespaces that controller is attached to, and then setting up 
        IO queues to perform operations on those namespaces. A namespace can be thought of as the storage device
        itself, and the controller is just a way to access it. Namespaces and controllers aren't required to be
        in a 1:1 setup, but I'm yet to come across that situation in a hobby os.

        There are a few different controller types, we're only interested in IO controllers. The others are not 
        useful to us. IO queues are for io operations and non exist by default. Admin queues have commands for 
        creating/destroying IO queues, as well as commands like identify (which we use to get info about things).

        Namespaces use LBA, and support multiple block sizes with various performance characteristics, 
        although you can choose to use just one. All this stuff is available from the identify namespace cmd.

        Queues are created in pairs: a submission queue and a completion queue. We submit commands to the former,
        and tell the controller. The controller then carries out the commands we submitted (maybe out of order),
        and posts entries in the completion queue for us to look at. The controller triggers an interrupt to
        tell us a completion entry has posted.

        If a command needs to return data, it will take pointers to physical pages (called prp = physical
        range/region pages). PRPs are the basic unit of data transfer between the driver and the controller.

        At the time of writing the osdev wiki page on NVMe looks useful, but it's totally useless for an actual
        implementation. Resources I found useful:
        -   Old version of Lyre (this is GPLv3 licensed)
            https://github.com/lyre-os/lyre/blob/old-lyre/kernel/dev/storage/nvme/nvme.c

        -   SerenityOS (BSD2 license)
            https://github.com/SerenityOS/serenity/tree/master/Kernel/Storage/NVMe
            
        -   NVM Express Base Specification
            Mainly chapters 3 (architecture) and 5 (admin command set). Chapter 2 is worth reading a few times to 
            understand the concepts behind NVMe.

        -   NVM Command Set Specification
            Details the NVM command set, which is the one you'll want to use. Very short, useful as a reference.
            
        -   NVM Express PCIe Transport Specification
            NVMe operates over a bunch of transports, this talks about the PCIe specific stuff. Also very short.
    */
    class NvmeController : public Drivers::GenericDriver
    {
    private:
        PciAddress address;
        PciCap* msi; //could be msi or msix, check the cap-id field before accessing.
        volatile NvmePropertyMap* properties;
        size_t doorbellStride;
        size_t maxQueueEntries;
        size_t maxTransferSize;

        sl::Vector<NvmeNamespace> namespaces;
        sl::Vector<NvmeQueue> queues;

        void Disable();
        bool Enable();
        void InitInterrupts(sl::Opt<PciCap*>& msi, sl::Opt<PciCap*>& msix);

        void CreateAdminQueue(size_t queueEntries);
        void DestroyAdminQueue();
        void CreateIoQueue(size_t index);
        void DestroyIoQueue(size_t index);

        //we run admin commands synchronously, as they're commonly only used during startup/shutdown. This
        NvmeCmdResult DoAdminCommand(SubmissionQueueEntry* cmd, CompletionQueueEntry* completion = nullptr);
        bool BuildPrps(NvmeQueue& queue, SubmissionQueueEntry& cmd, sl::BufferView buffer);
        size_t BeginIoCmd(size_t nsid, size_t lbaStart, sl::BufferView buffer, uint8_t opcode);
        sl::Opt<NvmeCmdResult> EndIoCmd(size_t operationId);

        [[nodiscard]]
        sl::BufferView Identify(IdentifyCns cns, uint32_t namespaceId, sl::BufferView buffer);
        size_t GetMaxIoQueueCount();

    public:
        void Init(Drivers::DriverInitInfo* initInfo) override;
        void Deinit() override;
        void HandleEvent(Drivers::DriverEventType type, void* args) override;

        //start a read operation from a namespace, returns an operation id that can be used to end it (the read op).
        //dest.base MUST be page-aligned, and length must be lba aligned or read will fail.
        size_t BeginRead(size_t nsid, size_t lbaStart, sl::BufferView dest);
        //returns an empty Opt<> if the operation is ongoing, otherwise returns the result.
        sl::Opt<NvmeCmdResult> EndRead(size_t operationId);
        size_t BeginWrite(size_t nsid, size_t lbaStart, sl::BufferView source);
        sl::Opt<NvmeCmdResult> EndWrite(size_t operationId);
    };

    class NvmeBlockDevice : public GenericBlock
    {
    friend NvmeController;
    private:
        NvmeController* driver;
        uint32_t nsid;

        NvmeBlockDevice(NvmeController* controller, uint32_t nsid)
        : driver(controller), nsid(nsid)
        {}
    
    protected:
        void Init() override;
        void Deinit() override;

    public:
        void Reset() override;
        sl::Opt<Drivers::GenericDriver*> GetDriverInstance() override;

        size_t BeginRead(size_t startLba, sl::BufferView dest) override;
        sl::Opt<BlockCmdResult> EndRead(size_t token) override;
        size_t BeginWrite(size_t startLba, sl::BufferView source) override;
        sl::Opt<BlockCmdResult> EndWrite(size_t token) override;
    };

    Drivers::GenericDriver* CreateNewNvmeDriver();
}
