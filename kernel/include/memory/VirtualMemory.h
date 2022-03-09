#pragma once

#include <NativePtr.h>
#include <memory/Paging.h>
#include <containers/LinkedList.h>

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

        sl::LinkedList<VMRange> InsertRange(NativeUInt base, size_t length, MemoryMapFlags flags);
        sl::LinkedList<VMRange> DestroyRange(NativeUInt base, size_t length);
        MemoryMapFlags MergeFlags(MemoryMapFlags a, MemoryMapFlags b);

    public:
        static VirtualMemoryManager* Current();
        void Init();
        PageTableManager& PageTables();

        void AddRange(NativeUInt base, size_t length, MemoryMapFlags flags);
        bool RemoveRange(NativeUInt base);
        bool RemoveRange(NativeUInt base, size_t length);
    };

    using VMM = VirtualMemoryManager;
}
