#pragma once

#include <Platform.h>
#include <Optional.h>
#include <BufferView.h>
#include <memory/MemoryFlags.h>

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

    FORCE_INLINE MemoryMapFlags operator&(const MemoryMapFlags& a, const MemoryMapFlags& b)
    { return static_cast<MemoryMapFlags>((uint64_t)a & (uint64_t)b);}

    FORCE_INLINE MemoryMapFlags operator|(const MemoryMapFlags& a, const MemoryMapFlags& b)
    { return static_cast<MemoryMapFlags>((uint64_t)a | (uint64_t)b);}
    
    class PageTableManager
    {
    private:
        static size_t pagingLevels;

        sl::NativePtr topLevelAddress;
        char lock;

        FORCE_INLINE
        void InvalidatePage(sl::NativePtr virtualAddr) const;
        
    public:
        static PageTableManager* Current();
        static void Setup();

        void InitKernelFromLimine(bool reuseBootloaderMaps = false);
        void InitClone();
        void Teardown(bool includeHigherHalf = false);

        void MakeActive() const;
        bool IsActive() const;
        bool PageSizeAvailable(PagingSize size) const;
        sl::NativePtr GetTla() const; //I'm going to regret exposing this pointer arent I?

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

        //removes a mapping from a virtual region, returns the size of attached page.
        PagingSize UnmapMemory(sl::NativePtr virtAddr, bool freePhysicalPage = true);
        
        PagingSize UnmapRange(sl::NativePtr virtAddrBase, size_t count, bool freePhysicalPages = true);
    };
}
