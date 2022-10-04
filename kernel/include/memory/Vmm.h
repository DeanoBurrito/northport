#pragma once

#include <containers/Vector.h>
#include <Optional.h>
#include <Locks.h>
#include <stddef.h>
#include <stdint.h>

namespace Npk::Memory
{
    enum VMFlags : size_t
    {
        None = 0,

        Writable = 1 << 0,
        Executable = 1 << 1,
        User = 1 << 2,
        Guarded = 1 << 3,
    };

    constexpr VMFlags operator|(const VMFlags& a, const VMFlags& b)
    { return (VMFlags)((uintptr_t)a | (uintptr_t)b); }

    constexpr VMFlags operator&(const VMFlags& a, const VMFlags& b)
    { return (VMFlags)((uintptr_t)a & (uintptr_t)b); }

    constexpr VMFlags operator|=(VMFlags& src, const VMFlags& other)
    { return src = (VMFlags)((uintptr_t)src | (uintptr_t)other); }

    constexpr VMFlags operator&=(VMFlags& src, const VMFlags& other)
    { return src = (VMFlags)((uintptr_t)src & (uintptr_t)other); }

    constexpr VMFlags operator~(const VMFlags& src)
    { return (VMFlags)(~(uintptr_t)src); }

    struct VMRange
    {
        uintptr_t base;
        size_t length;
        VMFlags flags;
        void* link;

        VMRange() : base(0), length(0), flags(VMFlags::None), link(nullptr)
        {}

        VMRange(uintptr_t base, size_t length) 
        : base(base), length(length), flags(VMFlags::None), link(nullptr)
        {}

        VMRange(uintptr_t base, size_t length, VMFlags flags) :
        base(base), length(length), flags(flags), link(nullptr)
        {}

        constexpr inline uintptr_t Top() const
        { return base + length; }
    };

    class VirtualMemoryManager;

    //badge pattern
    struct VmmKey
    {
    friend VirtualMemoryManager;
    private:
        VmmKey() = default;
    };

    class VirtualMemoryManager
    {
    private:
        sl::Vector<VMRange> ranges;
        void* ptRoot;
        sl::TicketLock lock;

        uintptr_t allocLowerLimit;
        uintptr_t allocUpperLimit;

        bool InsertRange(const VMRange& range);
    
    public:
        static void SetupKernel();
        static VirtualMemoryManager& Kernel();
        static VirtualMemoryManager& Current();

        VirtualMemoryManager();
        VirtualMemoryManager(VmmKey);

        ~VirtualMemoryManager();
        VirtualMemoryManager(const VirtualMemoryManager&) = delete;
        VirtualMemoryManager& operator=(const VirtualMemoryManager&) = delete;
        VirtualMemoryManager(VirtualMemoryManager&&) = delete;
        VirtualMemoryManager& operator=(VirtualMemoryManager&&) = delete;

        bool AddRange(const VMRange& range);
        bool RemoveRange(const VMRange& range);
        sl::Opt<VMRange> AllocRange(size_t length, VMFlags flags, uintptr_t lowerBound = 0, uintptr_t upperBound = -1ul);
        sl::Opt<VMRange> AllocRange(size_t length, VMFlags flags, uintptr_t physBase, uintptr_t lowerBound = 0, uintptr_t upperBound = -1ul);

        bool RangeExists(const VMRange& range, bool checkFlags) const;
    };
}

using VMM = Npk::Memory::VirtualMemoryManager;
using Npk::Memory::VMFlags;
