#include <devices/pci/VirtioQueue.h>
#include <memory/PhysicalMemory.h>

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

    void VirtioQueue::AllocBuffer(size_t queueSizeHint)
    {
        cfg->queueSelect = id;
        const size_t actualMax = sl::min(queueSizeHint, (size_t)cfg->queueSize);
        size = cfg->queueSize = actualMax;

        size_t totalSize = 0;
        totalSize += cfg->queueSize * 16;
        totalSize += cfg->queueSize * 2 + 6;
        totalSize += cfg->queueSize * 8 + 6;
        totalSize += 10; //worst-case misalignment we could have, this gives us some room for error
        
        totalSize = (totalSize / PAGE_FRAME_SIZE + 1);
        sl::NativePtr physicalBuffer = Memory::PMM::Global()->AllocPages(totalSize);

        descriptorTable = { physicalBuffer.raw, (size_t)cfg->queueSize * 16 };
        availableRing = { descriptorTable.base.raw + descriptorTable.length, (size_t)cfg->queueSize * 2 + 6 };
        availableRing.base = (availableRing.base.raw / 2 + 1) * 2;
        usedRing = { availableRing.base.raw + availableRing.length, (size_t)cfg->queueSize * 8 + 6};
        usedRing.base = (usedRing.base.raw / 8 + 1) * 8;

        //write back these addresses to the device queue config
        cfg->queueDesc = descriptorTable.base.raw;
        cfg->queueDriver = availableRing.base.raw;
        cfg->queueDevice = usedRing.base.raw;
    }

    void VirtioQueue::FreeBuffer()
    {
        //TODO: implement free buffers
    }

    bool VirtioQueue::IsEnabled()
    {
        cfg->queueSelect = id;
        return (cfg->queueEnable != 0);
    }

    void VirtioQueue::SetEnabled(bool yes)
    {
        cfg->queueSelect = id;
        cfg->queueEnable = yes ? 1 : 0;
    }
}
