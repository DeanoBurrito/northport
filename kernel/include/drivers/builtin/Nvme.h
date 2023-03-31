#pragma once

#include <drivers/builtin/NvmeDefs.h>
#include <devices/GenericDevices.h>
#include <devices/PciCapabilities.h>
#include <memory/VmObject.h>
#include <containers/Vector.h>
#include <Locks.h>

namespace Npk::Drivers
{
    void NvmeMain(void* arg);

    struct NvmeQ
    {
        volatile SqEntry* sq;
        volatile CqEntry* cq;
        volatile uint32_t* sqDoorbell;
        volatile uint32_t* cqDoorbell;
        size_t entries;
        size_t nextCmdId;
        size_t sqTail; //where we submit new entries at TODO: this should be atomic?
        size_t sqHead; //where the controller has processed up to.
        size_t cqHead; //where to look for new completions
        sl::TicketLock lock;
    };

    struct NvmeNamespace
    {
        size_t id;
        size_t lbaCount;
        size_t lbaSize;
        size_t deviceId;
    };

    union NvmeCmdToken
    {
        struct [[gnu::packed]]
        {
            uint16_t queueIndex;
            uint16_t cmdId;
            uint32_t cqHead; //head at time of posting command
        };
        uint64_t squished;
    };

    using NvmeResult = uint16_t;
    class NvmeBlockDevice;
    
    class NvmeController
    {
    friend NvmeBlockDevice;
    private:
        Devices::PciAddress addr;
        Memory::VmObject propsAccess;

        size_t doorbellStride;
        Devices::PciCap msiCap; //can be MSI or MSI-X, check type field.
        VmObject msixBirAccess;

        size_t ioQueueMaxSize;
        size_t maxTransferSize;
        sl::Vector<NvmeQ> queues;
        sl::TicketLock queuesLock;

        sl::Vector<NvmeNamespace> namespaces;
        sl::TicketLock namespacesLock;

        void Enable(bool yes);
        bool InitInterrupts();
        void DeinitInterrupts();

        bool CreateAdminQueue(size_t entries);
        bool DestroyAdminQueue();
        bool CreateIoQueue(size_t index, size_t entries);
        bool DestroyIoQueue(size_t index);

        bool IdentifyController();
        void DiscoverNamespaces();

        NvmeCmdToken BeginCmd(size_t queueIndex, const SqEntry& cmd);
        sl::Opt<NvmeResult> EndCmd(NvmeCmdToken token, bool block);
        void LogResult(NvmeResult result);
        
    public:
        bool Init(Devices::PciAddress pciAddr);
        bool Deinit();

        void UpdateQueues(size_t mask, size_t offset);
    };

    class NvmeBlockDevice : public Devices::GenericBlock
    {
    private:
        NvmeController& controller;
        NvmeNamespace& info;

    public:
        NvmeBlockDevice(NvmeController& ctrlr, NvmeNamespace& info) 
        : controller(ctrlr), info(info)
        {}

        bool Init() override;
        bool Deinit() override;

        void* BeginRead(size_t startLba, size_t lbaCount, sl::NativePtr buffer) override;
        bool EndRead(void* token) override;
        void* BeginWrite(size_t startLba, size_t lbaCount, sl::NativePtr buffer) override;
        bool EndWrite(void* token) override;
    };

    //utility functions to assist with PRP list management
    void BuildPrps(SqEntry& sqEntry, size_t length, sl::NativePtr buffer);
    void CleanupPrps(SqEntry& sqEntry, size_t length);
}
