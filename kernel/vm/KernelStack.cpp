#include <VmPrivate.hpp>

namespace Npk
{
    constexpr HeapTag KernelStackTag = NPK_MAKE_HEAP_TAG("Stck");

    VmStatus AllocKernelStack(void** stack)
    {
        void* ptr = PoolAllocWired(KernelStackSize(), KernelStackTag);
        if (ptr == nullptr)
            return VmStatus::Shortage;

        *stack = ptr;
        return VmStatus::Success;
    }

    void FreeKernelStack(void* stack)
    {
        auto succ = PoolFreeWired(stack, KernelStackSize(), KernelStackTag);

        NPK_ASSERT(succ);
    }
}
