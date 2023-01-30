#pragma once

#include <drivers/builtin/NvmeDefs.h>
#include <devices/GenericDevices.h>
#include <devices/PciAddress.h>
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
        sl::TicketLock lock;
    };
    
    class NvmeController
    {
    private:
        Devices::PciAddress addr;
        Memory::VmObject propsAccess;
        size_t doorbellStride;
        size_t ioQueueMaxSize;

        sl::Vector<NvmeQ> queues;
        sl::TicketLock queuesLock;

        void Enable(bool yes);
        bool CreateAdminQueue(size_t entries);
        bool DestroyAdminQueue();
        bool CreateIoQueue(size_t index);
        bool DestroyIoQueue(size_t index);

    public:
        bool Init(Devices::PciAddress pciAddr);
        bool Deinit();
    };
}
