#pragma once

#include <stdint.h>
#include <stddef.h>
#include <Locks.h>
#include <Maths.h>
#include <Optional.h>
#include <containers/LinkedList.h>
#include <memory/VmObject.h>
#include <Flags.h>

namespace Npk
{
    struct HatMap; //see arch/Hat.h for details, this struct is deliberately opaque.
}

namespace Npk::Memory
{
    struct VmRange : public sl::Intrusive::ListNode<VmRange> //TODO: smaller structure than double LL
    {
        uintptr_t base;
        size_t length;
        VmFlags flags;
        void* token;
        size_t offset;
        size_t reserved;

        constexpr inline uintptr_t Top() const
        { return base + length; }
    };

    /*  The VMM needs to dynamically allocate a number of VmRanges to operate, which presents
        a problem because the kernel heap depends on the VMM. These structs specifically are
        allocated by the following allocator.
        It's just a bitmap based slab allocator at heart, contained within a single page. A number 
        of can be chained in a linked list, in no particular order.
        We can easily create an instance of this allocator by getting a page from the PMM,
        and then adding the HHDM offset to it, allowing it to be accessed from any view of
        kernel memory.
    */
    constexpr size_t VmBitmapBytes = PageSize / sizeof(VmRange) / 8;
    constexpr size_t VmSlabCount = PageSize / sizeof(VmRange) - 
        (sl::AlignUp(VmBitmapBytes + sizeof(void*), sizeof(VmRange)) / sizeof(VmRange));
    struct VmSlabAlloc
    {
        VmSlabAlloc* next;
        uint8_t bitmap[VmBitmapBytes];
        alignas(sizeof(VmRange)) VmRange slabs[VmSlabCount];
    };

    static_assert(sizeof(VmSlabAlloc) == PageSize);

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

    class VirtualMemoryManager
    {
    private:
        sl::TicketLock rangesLock;
        sl::Intrusive::LinkedList<VmRange> ranges;
        sl::TicketLock allocLock;
        VmSlabAlloc* rangesAlloc;
        
        sl::TicketLock mapLock;
        HatMap* hatMap;

        uintptr_t globalLowerBound;
        uintptr_t globalUpperBound;

        VmRange* AllocStruct();
        void FreeStruct(VmRange* item);

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
        //TODO dumpRange() instead of Print()
        
        sl::Opt<uintptr_t> Alloc(size_t length, uintptr_t initArg, VmFlags flags, uintptr_t lowerBound = 0, uintptr_t upperBound = -1ul);
        bool Free(uintptr_t base);
        sl::Opt<VmFlags> GetFlags(uintptr_t base, size_t length) const;
        bool SetFlags(uintptr_t base, size_t length, VmFlags flags);
        bool MemoryExists(uintptr_t base, size_t length, sl::Opt<VmFlags> flags);
        sl::Opt<uintptr_t> GetPhysical(uintptr_t vaddr);

        size_t CopyIn(void* foreignBase, void* localBase, size_t length);
        size_t CopyOut(void* localBase, void* foreignBase, size_t length);
    };
}

using VMM = Npk::Memory::VirtualMemoryManager;
