#include <services/VmPagers.h>
#include <services/Vmm.h>
#include <Hhdm.h>
#include <Memory.h>

namespace Npk::Services
{
    constexpr size_t SimulatedDelaySpins = 10000;
    using SwapAlloc = sl::RangeAllocator<uintptr_t, size_t, Core::WiredHeapAllocator>;

    uintptr_t swapBase;
    size_t swapLength;
    sl::SpinLock swapAllocLock;
    SwapAlloc swapAlloc;

    static bool Reserve(size_t length, SwapKey* key)
    {
        sl::ScopedLock scopeLock(swapAllocLock);
        auto found = swapAlloc.Alloc(length);

        if (!found.HasValue())
            return false;

        key->index = *found;
        return true;
    }

    static void Unreserve(SwapKey key, size_t length)
    {
        sl::ScopedLock scopeLock(swapAllocLock);
        swapAlloc.Free(key.index, length);
    }

    static bool Read(SwapKey key, size_t offset, uintptr_t paddr)
    {
        const uintptr_t base = swapBase + (key.index << PfnShift());
        if (base < swapBase || base > swapBase + swapLength)
            return false;

        for (size_t i = 0; i < SimulatedDelaySpins; i++)
            sl::HintSpinloop();

        sl::MemCopy(reinterpret_cast<void*>(hhdmBase + paddr),
                reinterpret_cast<const void*>(base + offset), PageSize());
        return true;
    }

    static bool Write(SwapKey key, size_t offset, uintptr_t paddr)
    {
        const uintptr_t base = swapBase + (key.index << PfnShift());
        if (base < swapBase || base > swapBase + swapLength)
            return false;

        for (size_t i = 0; i < SimulatedDelaySpins; i++)
            sl::HintSpinloop();

        sl::MemCopy(reinterpret_cast<void*>(base + offset),
            reinterpret_cast<const void*>(hhdmBase + paddr), PageSize());
        return true;
    }

    SwapBackend badSwapBackend
    {
        .Reserve = Reserve,
        .Unreserve = Unreserve,
        .Read = Read,
        .Write = Write,
    };

    SwapBackend* InitBadSwap(uintptr_t physBase, size_t length)
    {
        swapBase = physBase;
        swapLength = length;
        new (&swapAlloc) SwapAlloc(physBase, length, PfnShift());

        return &badSwapBackend;
    }
}
