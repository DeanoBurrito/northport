#include <VmPrivate.hpp>
#include <Core.hpp>

namespace Npk
{
    VmStatus PrimeKernelMap(uintptr_t vaddr)
    {
        MmuWalkResult result;
        PageAccessRef ref;

        return Private::PrimeMapping(MyKernelMap(), vaddr, result, ref);
    }

    VmStatus SetKernelMap(uintptr_t vaddr, Paddr paddr, VmFlags flags)
    {
        return Private::SetMap(MyKernelMap(), vaddr, paddr, flags);
    }
}
