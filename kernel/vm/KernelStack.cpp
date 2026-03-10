#include <VmPrivate.hpp>

namespace Npk
{
    constexpr HeapTag KernelStackTag = NPK_MAKE_HEAP_TAG("Stck");

    NpkStatus AllocKernelStack(void** stack)
    {
        void* ptr = PoolAllocWired(KernelStackSize(), KernelStackTag);
        if (ptr == nullptr)
            return NpkStatus::Shortage;

        *stack = ptr;
        return NpkStatus::Success;
    }

    void FreeKernelStack(void* stack)
    {
        auto succ = PoolFreeWired(stack, KernelStackSize(), KernelStackTag);

        NPK_ASSERT(succ);
    }
}
