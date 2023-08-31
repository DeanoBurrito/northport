#pragma once

#include <stdint.h>
#include <stddef.h>
#include <Locks.h>
#include <Optional.h>
#include <memory/VmObject.h>
#include <containers/RBTree.h>
#include <Flags.h>

namespace Npk
{
    struct HatMap; //Just a forward decl for arch/Hat, so we dont include all it's crap here.
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
        bool DestroyMetaSlab(VmmMetaType type);
        void* AllocMeta(VmmMetaType type);
        void FreeMeta(void* ptr, VmmMetaType type);

        void CommonInit();
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

        //make this VMM's translations active for the current core.
        void MakeActive();
        //allows to VMM to respond to an attempt to access a memory address, returns if the access was valid or not.
        bool HandleFault(uintptr_t addr, VmFaultFlags flags);
        
        //allocates virtual memory and returns the base address of the allocated addresses.
        sl::Opt<uintptr_t> Alloc(size_t length, uintptr_t initArg, VmFlags flags, VmmAllocLimits = {});
        //frees virtual memory, returns whether freeing was successful or not.
        bool Free(uintptr_t base);
        //gets the flags associated with a range of virtual memory.
        sl::Opt<VmFlags> GetFlags(uintptr_t base, size_t length = 0);
        //attempts to update the flags for a range of virtual memory, returns whether the operation
        //was successful or not. The operation is atomic, so there is no 'partial flags update':
        //the flags will match what was requested, or they will remain the same.
        bool SetFlags(uintptr_t base, VmFlags flags);

        //checks if some virtual memory exists, and can optionally check it's permissions
        //and type via the flags argument.
        bool MemoryExists(uintptr_t base, size_t length, sl::Opt<VmFlags> flags);
        //tries to translate a virtual address via this VMM into a physical address. If virtual memory
        //is present at the address but not backed, this function won't cause a page fault to trigger
        //backing, that must be done manually if required.
        sl::Opt<uintptr_t> GetPhysical(uintptr_t vaddr);

        //copies data from the current address space into the one managed by this VMM.
        //If virtual memory in this VMM isn't backed, it will trigger a page fault to
        //provide backing before continuing the copy.
        size_t CopyIn(void* foreignBase, void* localBase, size_t length);
        //copies data from this VMM's address space into the current one. This function
        //won't manually trigger page faults if the destination memory doesn't exist, 
        //it honours whatever behaviour the platform uses.
        size_t CopyOut(void* localBase, void* foreignBase, size_t length);
    };
}

using VMM = Npk::Memory::VirtualMemoryManager;
