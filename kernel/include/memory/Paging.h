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

    enum class MemoryMapFlags : size_t
    {
        None = 0,
        AllowWrites = 1 << 0,
        AllowExecute = 1 << 1,
        UserAccessible = 1 << 2,
        SystemRegion = 1 << 3,
    };

    FORCE_INLINE MemoryMapFlags operator&(const MemoryMapFlags& a, const MemoryMapFlags& b)
    { return static_cast<MemoryMapFlags>((uint64_t)a & (uint64_t)b);}

    FORCE_INLINE MemoryMapFlags operator|(const MemoryMapFlags& a, const MemoryMapFlags& b)
    { return static_cast<MemoryMapFlags>((uint64_t)a | (uint64_t)b);}
    
    class PageTableManager
    {
    private:
        static bool usingExtendedPaging;

        sl::NativePtr topLevelAddress;
        char lock;

        FORCE_INLINE
        void InvalidatePage(sl::NativePtr virtualAddr) const;
        
    public:
        static PageTableManager* Current();
        static void Setup();

        void InitKernel(bool reuseBootloaderMaps = false);
        void InitClone();

        void MakeActive() const;
        bool IsActive() const;
        bool PageSizeAvailable(PagingSize size) const;

        bool ModifyPageFlags(sl::NativePtr virtAddr, MemoryMapFlags flags, size_t appliedLevelsBitmap);
        sl::Opt<sl::NativePtr> GetPhysicalAddress(sl::NativePtr virtAddr);

        //allocates a physical page and maps it at the specified address.
        void MapMemory(sl::NativePtr virtAddr, MemoryMapFlags flags);
        //maps the supplied physical address to the virtual address.
        void MapMemory(sl::NativePtr virtAddr, sl::NativePtr physAddr, MemoryMapFlags flags);
        //maps a region of memory of pageSize at the address supplied. Check which sizes are available before using this.
        void MapMemory(sl::NativePtr virtAddr, PagingSize pageSize, MemoryMapFlags flags);
        void MapMemory(sl::NativePtr virtAddr, sl::NativePtr physAddr, PagingSize pageSize, MemoryMapFlags flags);

        void MapRange(sl::NativePtr virtAddrBase, size_t count, MemoryMapFlags flags);
        void MapRange(sl::NativePtr virtAddrBase, sl::NativePtr physAddrBase, size_t count, MemoryMapFlags flags);
        void MapRange(sl::NativePtr virtAddrBase, size_t count, PagingSize pageSize, MemoryMapFlags flags);
        void MapRange(sl::NativePtr virtAddrBase, sl::NativePtr physAddrBase, size_t count, PagingSize pageSize, MemoryMapFlags flags);

        //removes a mapping froma virtual region, returns the size of attached page.
        PagingSize UnmapMemory(sl::NativePtr virtAddr, bool freePhysicalPage = true);
        
        PagingSize UnmapRange(sl::NativePtr virtAddrBase, size_t count, bool freePhysicalPages = true);
    };
}
