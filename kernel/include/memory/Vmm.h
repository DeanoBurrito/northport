#pragma once

#include <stdint.h>
#include <stddef.h>
#include <Locks.h>
#include <Optional.h>
#include <containers/LinkedList.h>
#include <memory/Heap.h>

namespace Npk::Memory
{
    enum class VmFlags : size_t
    {
        None = 0,

        //access flags
        Write = 1 << 0,
        Execute = 1 << 1,
        User = 1 << 2,

        //bits 48-63 are the type of memory to be requested
        Anon = 1ul << 48,
        Mmio = 2ul << 48,
    };

    constexpr VmFlags operator|(const VmFlags& a, const VmFlags& b)
    { return (VmFlags)((uintptr_t)a | (uintptr_t)b); }

    constexpr VmFlags operator&(const VmFlags& a, const VmFlags& b)
    { return (VmFlags)((uintptr_t)a & (uintptr_t)b); }

    constexpr VmFlags operator|=(VmFlags& src, const VmFlags& other)
    { return src = (VmFlags)((uintptr_t)src | (uintptr_t)other); }

    constexpr VmFlags operator&=(VmFlags& src, const VmFlags& other)
    { return src = (VmFlags)((uintptr_t)src & (uintptr_t)other); }

    constexpr VmFlags operator~(const VmFlags& src)
    { return (VmFlags)(~(uintptr_t)src); }

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
        
        sl::Opt<VmRange> Alloc(size_t length, uintptr_t initArg, VmFlags flags, uintptr_t lowerBound = 0, uintptr_t upperBound = -1ul);
        bool Free(uintptr_t base);

        bool RangeExists(uintptr_t base, size_t length, sl::Opt<VmFlags> flags);

        size_t CopyIn(void* foreignBase, void* localBase, size_t length);
        size_t CopyOut(void* localBase, void* foreignBase, size_t length);
    };
}

using VMM = Npk::Memory::VirtualMemoryManager;
using Npk::Memory::VmFlags;
