#include <devices/pci/VirtioQueue.h>
#include <devices/pci/PciCommon.h>

namespace Kernel::Devices::Pci
{
    sl::Vector<VirtioQueue> VirtioQueue::FindQueues(VirtioPciCommonConfig* config)
    {
        volatile VirtioPciCommonConfig* cfg = config;
        sl::Vector<VirtioQueue> queues;

        for (size_t i = 0; i < cfg->numberOfQueues; i++)
        {
            queues.EmplaceBack();
            cfg->queueSelect = i;

            queues[i].id = i;
            queues[i].size = cfg->queueSize;
            queues[i].cfg = cfg;
        }

        return queues;
    }

    VirtioQueue::~VirtioQueue()
    {
        if (descriptorTable.base.ptr != nullptr)
            FreeBuffer();
    }

    void VirtioQueue::AllocBuffer(size_t minQueueSize, size_t maxQueueSize)
    {
        
    }

    void VirtioQueue::FreeBuffer()
    {}
}
