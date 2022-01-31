#pragma once

#include <NativePtr.h>
#include <Platform.h>
#include <Optional.h>

namespace Kernel::Memory
{   
    enum class PagingSize : size_t
    {
        //NOTE: is it illegal to try to allocate pages of this size, this is for returns only
        NoSize = 0,

        Physical = PAGE_FRAME_SIZE,

        _4KB = 0x1000,
        _2MB = 0x200000,
        _1GB = 0x40000000,
    };

    enum class MemoryMapFlag : size_t
    {
        None = 0,
        AllowWrites = 1 << 0,
        AllowExecute = 1 << 1,
        UserAccessible = 1 << 2,
    };

    FORCE_INLINE MemoryMapFlag operator&(const MemoryMapFlag& a, const MemoryMapFlag& b)
    { return static_cast<MemoryMapFlag>((uint64_t)a & (uint64_t)b);}

    FORCE_INLINE MemoryMapFlag operator|(const MemoryMapFlag& a, const MemoryMapFlag& b)
    { return static_cast<MemoryMapFlag>((uint64_t)a | (uint64_t)b);}
    
    class PageTableManager
    {
    private:
        static bool usingExtendedPaging;

        sl::NativePtr topLevelAddress;
        char lock;

        FORCE_INLINE
        void InvalidatePage(sl::NativePtr virtualAddr) const;
        
    public:
        static PageTableManager* Local();
        static void Setup();

        void InitKernel(bool reuseBootloaderMaps = false);
        void InitClone();

        void MakeActive() const;
        bool IsActive() const;
        bool PageSizeAvailable(PagingSize size) const;

        //checks each level of the paging structure has the appropriate flags to allow access (execute/write/user)
        bool EnsurePageFlags(sl::NativePtr virtAddr, MemoryMapFlag flags, bool overwriteExisting = false);

        sl::Opt<sl::NativePtr> GetPhysicalAddress(sl::NativePtr virtAddr);

        //allocates a physical page and maps it at the specified address.
        void MapMemory(sl::NativePtr virtAddr, MemoryMapFlag flags);
        //maps the supplied physical address to the virtual address.
        void MapMemory(sl::NativePtr virtAddr, sl::NativePtr physAddr, MemoryMapFlag flags);
        //maps a region of memory of pageSize at the address supplied. Check which sizes are available before using this.
        void MapMemory(sl::NativePtr virtAddr, PagingSize pageSize, MemoryMapFlag flags);
        void MapMemory(sl::NativePtr virtAddr, sl::NativePtr physAddr, PagingSize pageSize, MemoryMapFlag flags);

        void MapRange(sl::NativePtr virtAddrBase, size_t count, MemoryMapFlag flags);
        void MapRange(sl::NativePtr virtAddrBase, sl::NativePtr physAddrBase, size_t count, MemoryMapFlag flags);
        void MapRange(sl::NativePtr virtAddrBase, size_t count, PagingSize pageSize, MemoryMapFlag flags);
        void MapRange(sl::NativePtr virtAddrBase, sl::NativePtr physAddrBase, size_t count, PagingSize pageSize, MemoryMapFlag flags);

        //removes a mapping froma virtual region, returns the size of attached page.
        PagingSize UnmapMemory(sl::NativePtr virtAddr);

        PagingSize UnmapRange(sl::NativePtr virtAddrBase, size_t count);
    };
}
