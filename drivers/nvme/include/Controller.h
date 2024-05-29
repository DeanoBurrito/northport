#pragma once

#include <NvmeSpecDefs.h>
#include <PciAddress.h>
#include <PciCapabilities.h>
#include <VmObject.h>
#include <Locks.h>
#include <containers/Vector.h>
#include <interfaces/driver/Api.h>
#include <interfaces/driver/Interrupts.h>
#include <interfaces/driver/Drivers.h>

namespace Nvme
{
    struct NvmeQueue
    {
        sl::SpinLock lock;
        sl::NativePtr sq;
        sl::NativePtr cq;
        volatile uint32_t* sqDoorbell;
        volatile uint32_t* cqDoorbell;
        size_t cqHead;
        size_t sqTail;
        size_t entries;
        uint16_t nextCmdId;
        uint8_t phase;
    };

    struct CmdToken
    {};

    struct NvmeNamespace
    {
        uint64_t blockCount;
        uint32_t id;
        uint32_t blockSize;
        uint16_t metadataSize;

        npk_io_device_api ioApi;
        sl::StringSpan summaryStr;
    };

    class NvmeController
    {
    private:
        dl::PciAddress pciAddr;
        dl::VmObject propsVmo;
        size_t doorbellStride;
        size_t ioQueueMaxEntries;
        size_t pageSize;
        size_t maxTransferSize;

        sl::RwLock queuesLock;
        sl::Vector<NvmeQueue> queues;
        sl::Vector<NvmeNamespace> namespaces;

        dl::MsiCapability msi;
        npk_dpc dpc;
        npk_interrupt_route intrRoute;

        bool Enable(bool yes);
        bool IdentifyController();
        bool IdentifyNamespaces();

        bool CreateAdminQueue(size_t entries);
        bool DestroyAdminQueue();
        sl::Opt<size_t> CreateIoQueue(size_t entries);
        bool DestroyIoQueue(size_t index);

        bool PollCommand(size_t queue, SqEntry& cmd, bool updateQueue);
        CmdToken BeginCommand(size_t queue, SqEntry& cmd);
        bool EndCommand(CmdToken token);

    public:
        bool Init(const npk_event_add_device* event);
        bool Deinit();
        void DpcCallback();
    };
}
