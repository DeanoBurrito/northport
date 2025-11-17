#include <VmPrivate.hpp>
#include <Maths.hpp>
#include <UnitConverter.hpp>

namespace Npk
{
    constexpr HeapTag SpaceHeapTag = NPK_MAKE_HEAP_TAG("Spac");

    void InitKernelVmSpace(uintptr_t lowBase, size_t lowLen, uintptr_t highBase,
        size_t highLen)
    {
        /* The kernel address space operates as a few distinct parts:
         * - pool space: provides the general purpose (read: malloc) allocators.
         * - cache space: used to map and access views of cached files.
         * - system space: general purpose address space, for loading drivers,
         *   accessing pinned (userspace/dma) buffers, other uses.
         * `AllocateInSpace()` will only return addresses from system space,
         * (there are other APIs to get addresses in the other spaces), but
         * all spaces uses the same `VmSpace`, returned from `KernelSpace()`.
         *
         * The size of the pool and cache space zones is calculated here and
         * fixed, system space is whatever is left over.
         */

        NPK_ASSERT((lowBase & PageMask()) == 0);
        NPK_ASSERT((highBase & PageMask()) == 0);

        const uintptr_t poolBase = lowBase;
        const size_t poolSize = AlignDownPage(lowLen / 4);
        Private::InitPool(poolBase, poolSize);

        auto conv = sl::ConvertUnits(poolSize);
        Log("Pool space: 0x%tx-0x%tx (%zu.%zu %sB)",
            LogLevel::Verbose, poolBase, poolBase + poolSize,
            conv.major, conv.minor, conv.prefix);

        const uintptr_t cacheBase = poolBase + poolSize;
        const size_t cacheSize = AlignDownPage(lowLen / 4);

        conv = sl::ConvertUnits(cacheSize);
        Log("Cache space: 0x%tx-0x%tx (%zu.%zu %sB)",
            LogLevel::Verbose, cacheBase, cacheBase + cacheSize,
            conv.major, conv.minor, conv.prefix);

        const uintptr_t systemLowBase = cacheBase + cacheSize;
        const size_t systemLowSize = AlignDownPage(lowLen / 2);

        conv = sl::ConvertUnits(systemLowSize);
        Log("General space (low): 0x%tx-0x%tx (%zu.%zu %sB)",
            LogLevel::Verbose, systemLowBase, systemLowBase + systemLowSize,
            conv.major, conv.minor, conv.prefix);

        conv = sl::ConvertUnits(highLen);
        Log("General space (high): 0x%tx-0x%tx (%zu.%zu %sB)",
            LogLevel::Verbose, highBase, highBase + highLen,
            conv.major, conv.minor, conv.prefix);
    }

    VmStatus AllocateInSpace(VmSpace& space, void** addr, size_t length, size_t align)
    { NPK_UNREACHABLE(); }
    VmStatus FreeInSpace(VmSpace& space, void* base, size_t length)
    { NPK_UNREACHABLE(); }
}
