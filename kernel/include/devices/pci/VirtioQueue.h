#pragma once

#include <devices/pci/VirtioCommon.h>
#include <devices/pci/PciAddress.h>
#include <containers/Vector.h>

namespace Kernel::Devices::Pci
{
    //NOTE: this class operates directly on the pci config space, you will need to lock around that yourself in the driver.
    struct VirtioQueue
    {
    private:
        volatile VirtioPciCommonConfig* cfg;

    public:
        size_t id;
        size_t size;
        
        //these buffers point to physical memory via the hhdm
        sl::BufferView descriptorTable;
        sl::BufferView availableRing;
        sl::BufferView usedRing;

        //finds all virtqueues, does not allocate memory.
        static sl::Vector<VirtioQueue> FindQueues(VirtioPciCommonConfig* config);

        ~VirtioQueue();

        void AllocBuffer(size_t minQueueSize, size_t maxQueueSize);
        void FreeBuffer();
    };
}
