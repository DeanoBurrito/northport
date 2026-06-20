#include <private/Vm.hpp>

namespace Npk
{
    constexpr HeapTag KernelStackTag = NPK_MAKE_HEAP_TAG("Stck");

    NpkStatus AllocKernelStack(void** stack)
    {
        void* ptr = PoolAllocWired(KernelStackSize(), KernelStackTag);
        if (ptr == nullptr)
            return NpkStatus::Shortage;

        auto addr = reinterpret_cast<uintptr_t>(ptr);
        addr += KernelStackSize();
        ptr = reinterpret_cast<void*>(addr);

        *stack = ptr;
        return NpkStatus::Success;
    }

    void FreeKernelStack(void* stack)
    {
        auto addr = reinterpret_cast<uintptr_t>(stack);
        addr -= KernelStackSize();
        stack = reinterpret_cast<void*>(addr);

        auto succ = PoolFreeWired(stack, KernelStackSize(), KernelStackTag);

        NPK_ASSERT(succ);
    }
}
