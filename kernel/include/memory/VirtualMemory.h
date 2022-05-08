#pragma once

#include <NativePtr.h>
#include <memory/Paging.h>
#include <containers/LinkedList.h>
#include <containers/Vector.h>

namespace Kernel::Memory
{
    struct VMRange
    {
    public:
        MemoryMapFlags flags;
        NativeUInt base;
        size_t length;

        VMRange() : flags(MemoryMapFlags::None), base(0), length(0)
        {}

        VMRange(MemoryMapFlags flags, NativeUInt base, size_t length) : flags(flags), base(base), length(length)
        {}
    };

    class VirtualMemoryManager
    {
    private:
        PageTableManager pageTables;
        sl::LinkedList<VMRange> ranges;

        sl::Vector<VMRange> InsertRange(NativeUInt base, size_t length, MemoryMapFlags flags);
        sl::Vector<VMRange> DestroyRange(NativeUInt base, size_t length);

    public:
        static VirtualMemoryManager* Current();
        void Init();
        void Deinit();
        PageTableManager& PageTables();

        void AddRange(NativeUInt base, size_t length, MemoryMapFlags flags);
        bool RemoveRange(NativeUInt base);
        bool RemoveRange(NativeUInt base, size_t length);

        sl::NativePtr AllocateRange(size_t length, MemoryMapFlags flags);
        sl::NativePtr AllocateRange(sl::NativePtr physicalBase, size_t length, MemoryMapFlags flags);

        sl::NativePtr AddSharedPhysicalRange(VirtualMemoryManager* foreignVmm, sl::NativePtr foreignAddr, size_t length, MemoryMapFlags localFlags);

        bool RangeExists(NativeUInt base, size_t length);
        bool RangeExists(NativeUInt base, size_t length, MemoryMapFlags minimumFlags);

        //print the current ranges to the log, highlighting a range that contains a target address is one existss.
        void PrintLog(sl::NativePtr highlightRangeOf = nullptr);
    };

    using VMM = VirtualMemoryManager;
}
