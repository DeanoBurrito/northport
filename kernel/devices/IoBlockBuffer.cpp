#include <devices/IoBlockBuffer.h>
#include <memory/PhysicalMemory.h>

namespace Kernel::Devices
{
    IoBlockBuffer::IoBlockBuffer(size_t pages)
    {
        memory.base = EnsureHigherHalfAddr(Memory::PMM::Global()->AllocPages(pages));
        memory.length = pages * PAGE_FRAME_SIZE;
    }

    IoBlockBuffer::~IoBlockBuffer()
    {
        Memory::PMM::Global()->FreePages(EnsureLowerHalfAddr(memory.base.ptr), memory.length / PAGE_FRAME_SIZE);
    }
}
