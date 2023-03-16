#pragma once

#include <stdint.h>
#include <stddef.h>
#include <Locks.h>
#include <Optional.h>
#include <containers/LinkedList.h>
#include <memory/Heap.h>
#include <memory/VmObject.h>

namespace Npk::Memory
{
    struct VmRange
    {
        uintptr_t base;
        size_t length;
        VmFlags flags;
        size_t token;

        constexpr inline uintptr_t Top() const
        { return base + length; }
    };

    enum class VmFaultFlags : uintptr_t
    {
        None = 0,
        Read = 1 << 0,
        Write = 1 << 1,
        Execute = 1 << 2,
        User = 1 << 3,
    };

    constexpr VmFaultFlags operator|(const VmFaultFlags& a, const VmFaultFlags& b)
    { return (VmFaultFlags)((uintptr_t)a | (uintptr_t)b); }

    constexpr VmFaultFlags operator&(const VmFaultFlags& a, const VmFaultFlags& b)
    { return (VmFaultFlags)((uintptr_t)a & (uintptr_t)b); }

    constexpr VmFaultFlags operator|=(VmFaultFlags& src, const VmFaultFlags& other)
    { return src = (VmFaultFlags)((uintptr_t)src | (uintptr_t)other); }

    //badge pattern
    class VirtualMemoryManager;
    class VmmKey
    {
    friend VirtualMemoryManager;
        VmmKey() = default;
    };

    class VirtualMemoryManager
    {
    private:
        sl::LinkedList<VmRange, PinnedAllocator> ranges;
        sl::TicketLock rangesLock;
        sl::TicketLock ptLock;
        void* ptRoot;
        uint32_t localKernelGen;

        uintptr_t globalLowerBound;
        uintptr_t globalUpperBound;

    public:
        static void InitKernel();
        static VirtualMemoryManager& Kernel();
        static VirtualMemoryManager& Current();

        VirtualMemoryManager();
        VirtualMemoryManager(VmmKey);

        ~VirtualMemoryManager();
        VirtualMemoryManager(const VirtualMemoryManager&) = delete;
        VirtualMemoryManager& operator=(const VirtualMemoryManager&) = delete;
        VirtualMemoryManager(VirtualMemoryManager&&) = delete;
        VirtualMemoryManager& operator=(VirtualMemoryManager&&) = delete;

        void MakeActive();
        void HandleFault(uintptr_t addr, VmFaultFlags flags);
        void PrintRanges(void (*PrintFunc)(const char* format, ...));
        
        sl::Opt<VmRange> Alloc(size_t length, uintptr_t initArg, VmFlags flags, uintptr_t lowerBound = 0, uintptr_t upperBound = -1ul);
        bool Free(uintptr_t base);

        bool RangeExists(uintptr_t base, size_t length, sl::Opt<VmFlags> flags);
        sl::Opt<uintptr_t> GetPhysical(uintptr_t vaddr);

        size_t CopyIn(void* foreignBase, void* localBase, size_t length);
        size_t CopyOut(void* localBase, void* foreignBase, size_t length);
    };
}

using VMM = Npk::Memory::VirtualMemoryManager;
