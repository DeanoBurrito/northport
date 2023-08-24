#pragma once

#include <stdint.h>
#include <stddef.h>
#include <Locks.h>
#include <Maths.h>
#include <Optional.h>
#include <memory/VmObject.h>
#include <containers/RBTree.h>
#include <Flags.h>
#include <Span.h>

namespace Npk
{
    struct HatMap; //see arch/Hat.h for details, this struct is deliberately opaque.
}

namespace Npk::Memory
{
    struct VmRange
    {
        uintptr_t base;
        size_t length;
        VmFlags flags;
        size_t offset;
        void* token;

        sl::RBTreeHook hook;

        constexpr inline uintptr_t Top() const
        { return base + length; }
    };

    struct VmRangeLess
    {
        bool operator()(const VmRange& a, const VmRange& b)
        { return a.base < b.base; }
    };

    using VmRangeTree = sl::RBTree<VmRange, &VmRange::hook, VmRangeLess>;

    struct VmHole
    {
        uintptr_t base;
        size_t length;
        size_t largestHole;

        sl::RBTreeHook hook;
    };

    struct VmHoleLess
    {
        bool operator()(const VmHole& a, const VmHole& b)
        { return a.base < b.base; }
    };

    struct VmHoleAggregator;
    using VmHoleTree = sl::RBTree<VmHole, &VmHole::hook, VmHoleLess, VmHoleAggregator>;

    struct VmHoleAggregator
    {
        static bool Aggregate(VmHole* hole);
        static bool CheckInvariant(VmHoleTree& tree, VmHole* hole);
    };

    enum class VmmMetaType : size_t
    {
        Range,
        Hole,

        Count
    };

    struct VmmMetaSlab
    {
        VmmMetaSlab* next;
        uint8_t* bitmap;
        uintptr_t data;
        size_t free;
        size_t total;
    };

    struct VmmDebugEntry
    {
        uintptr_t base;
        size_t length;
        VmFlags flags;
        size_t lengthMappped;
    };

    enum class VmFaultFlag
    {
        Read = 0,
        Write = 1,
        Execute = 2,
        User = 3,
    };

    using VmFaultFlags = sl::Flags<VmFaultFlag>;

    //badge pattern
    class VirtualMemoryManager;
    class VmmKey
    {
    friend VirtualMemoryManager;
        VmmKey() = default;
    };

    struct VmmAllocLimits
    {
        uintptr_t lowerBound;
        uintptr_t upperBound;

        constexpr VmmAllocLimits() : lowerBound(0), upperBound(-1ul)
        {}
    };

    class VirtualMemoryManager
    {
    private:
        sl::TicketLock rangesLock;
        VmRangeTree ranges;
        sl::TicketLock holesLock;
        VmHoleTree holes;
        sl::TicketLock allocLock;

        VmmMetaSlab* metaSlabs[(size_t)VmmMetaType::Count];
        sl::TicketLock metaSlabLocks[(size_t)VmmMetaType::Count];
        
        sl::TicketLock mapLock;
        HatMap* hatMap;

        uintptr_t globalLowerBound;
        uintptr_t globalUpperBound;

        VmmMetaSlab* CreateMetaSlab(VmmMetaType type);
        void* AllocMeta(VmmMetaType type);
        void FreeMeta(void* ptr, VmmMetaType type);

        void AdjustHole(VmHole* target, size_t offset, size_t length);
        VmRange* FindRange(uintptr_t addr);

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
        bool HandleFault(uintptr_t addr, VmFaultFlags flags);
        
        sl::Opt<uintptr_t> Alloc(size_t length, uintptr_t initArg, VmFlags flags, VmmAllocLimits = {});
        bool Free(uintptr_t base);
        sl::Opt<VmFlags> GetFlags(uintptr_t base, size_t length = 0);
        bool SetFlags(uintptr_t base, size_t length, VmFlags flags);

        bool MemoryExists(uintptr_t base, size_t length, sl::Opt<VmFlags> flags);
        sl::Opt<uintptr_t> GetPhysical(uintptr_t vaddr);
        size_t GetDebugData(sl::Span<VmmDebugEntry>& entries, size_t offset = 0);

        size_t CopyIn(void* foreignBase, void* localBase, size_t length);
        size_t CopyOut(void* localBase, void* foreignBase, size_t length);
    };
}

using VMM = Npk::Memory::VirtualMemoryManager;
