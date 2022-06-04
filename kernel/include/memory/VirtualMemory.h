#pragma once

#include <BufferView.h>
#include <memory/Paging.h>
#include <containers/LinkedList.h>

namespace Kernel::Memory
{
    struct VMRange
    {
        NativeUInt base;
        size_t length;
        MemoryMapFlags flags;

        VMRange() : base(0), length(0), flags(MemoryMapFlags::None)
        {}

        VMRange(NativeUInt base, size_t length) : base(base), length(length), flags(MemoryMapFlags::None)
        {}

        VMRange(NativeUInt base, size_t length, MemoryMapFlags flags) 
        : base(base), length(length), flags(flags)
        {}

        FORCE_INLINE sl::BufferView ToView()
        { return { base, length }; }
    };

    class VirtualMemoryManager
    {
    friend PageTableManager;
    private:
        char rangesLock;
        sl::LinkedList<VMRange> ranges;
        PageTableManager pageTables;

        VMRange InsertRange(VMRange range, bool backImmediately);
        VMRange FindRange(size_t length, NativeUInt lowerBound, NativeUInt upperBound);
        
        size_t DoMemoryOp(sl::BufferView sourceBuffer, sl::NativePtr destBase, bool isCopy);

    public:
        static VirtualMemoryManager* Current();
        void Init();
        void Deinit();

        void MakeActive();
        bool IsActive() const;

        bool AddRange(VMRange range, bool backImmediately);
        bool RemoveRange(VMRange range);
        void ModifyRange(VMRange range, MemoryMapFlags newFlags);
        void ModifyRange(VMRange range, int adjustLength, bool fromEnd);

        //allocates a range big enough, optionally within a selected range.
        VMRange AllocRange(size_t length, bool backImmediately, MemoryMapFlags flags, NativeUInt lowerBound = 0, NativeUInt upperBound = (NativeUInt)-1);
        //allocates a range, but backed with a specific range of physical memory.
        VMRange AllocMmioRange(sl::NativePtr physBase, size_t length, MemoryMapFlags flags);

        //copies a range from another vmm, maps to the same physical memory (i.e. it's shared).
        VMRange AddSharedRange(VirtualMemoryManager& foreignVmm, VMRange foreignRange);
        //tries to convert a virtual address to a physical one.
        sl::Opt<sl::NativePtr> GetPhysAddr(sl::NativePtr ptr);
        
        //copies data from the current address space into this vmm, at the specified virt addr.
        //NOTE: This uses accesses physical memory directly, so it ignores read-only + user flags.
        size_t CopyInto(sl::BufferView sourceBuffer, sl::NativePtr destBase);
        //copies data from this vmm, to the current vmm. It uses hhdm to access pages directly, ignoring read-only + use flags
        size_t CopyFrom(sl::BufferView sourceBuffer, sl::NativePtr destBase);
        //zeroes a range of memory in this vmm, returns number of bytes zeroed.
        size_t ZeroRange(sl::BufferView where);

        bool RangeExists(VMRange range);
        bool RangeExists(VMRange range, MemoryMapFlags minimumFlags);

        void PrintRanges(sl::NativePtr highlightRangeOf = nullptr);
    };

    using VMM = VirtualMemoryManager;
}
