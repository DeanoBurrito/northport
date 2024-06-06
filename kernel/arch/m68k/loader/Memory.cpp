#include "Memory.h"
#include "Util.h"
#include <NativePtr.h>
#include <Maths.h>
#include <containers/List.h>
#include <Memory.h>

namespace Npl
{
    struct MemoryBlock
    {
        MemoryBlock* next;
        uintptr_t base;
        size_t length;
        MemoryType type;
    };

    constexpr uint32_t PteResident = 3 << 0;
    constexpr uint32_t PteWrite = 1 << 2;
    constexpr uint32_t PteCopyback = 1 << 5;
    constexpr uint32_t PteSupervisor = 1 << 7;

    struct PageTable
    {
        uint32_t entries[128];
    };
    
    constexpr size_t FreeHeaderCount = 256;
    MemoryBlock freeHeaders[FreeHeaderCount];
    size_t freeHeadersUsed;

    sl::IntrFwdList<MemoryBlock> physList {};
    uintptr_t ptRoot;

    struct MemoryBlockComparer
    {
        bool operator()(const MemoryBlock& a, const MemoryBlock& b)
        { return a.base > b.base; }
    };

    static MemoryBlock* AllocMemoryBlock()
    {
        MemoryBlock* block = &freeHeaders[freeHeadersUsed++];
        if (freeHeadersUsed == FreeHeaderCount)
            Panic(PanicReason::InternalAllocFailure);
        return block;
    }

    static void ReservePhysicalRange(uintptr_t base, size_t top, MemoryType type)
    {
        base = sl::AlignDown(base, PageSize);
        top = sl::AlignUp(top, PageSize);

        for (MemoryBlock* scan = physList.Begin(); scan != nullptr; scan = scan->next)
        {
            if (scan->type != MemoryType::Usable)
                continue; //we only care about reserving usable memory

            const uintptr_t scanTop = scan->base + scan->length;
            if (scan->length == 0 || top < scan->base || base >= scanTop)
                continue; //no overlap, ignore this block

            if (base <= scan->base) //reserved overlaps beginning of scan
            {
                if (top >= scanTop)
                    scan->length = 0;
                else
                {
                    scan->base = top;
                    scan->length = scanTop - scan->base;
                }
            }
            else if (top > scanTop) //reserved overlaps end of scan
                scan->length = base - scan->base;
            else //reserved is in the middle of scan
            {
                scan->length = base - scan->base;

                MemoryBlock* latest = AllocMemoryBlock();
                latest->type = MemoryType::Usable;
                latest->base = top;
                latest->length = scanTop - top;
                physList.PushBack(latest);
            }
        }

        MemoryBlock* reserved = AllocMemoryBlock();
        reserved->type = type;
        reserved->base = base;
        reserved->length = top - base;
        physList.PushBack(reserved);

        physList.Sort(MemoryBlockComparer{});
    }

    static uintptr_t AllocPage()
    {
        MemoryBlock* firstUsable = nullptr;
        for (MemoryBlock* scan = physList.Begin(); scan != nullptr; scan = scan->next)
        {
            if (firstUsable == nullptr && scan->type == MemoryType::Usable)
                firstUsable = scan;

            if (scan->type != MemoryType::Reclaimable || scan->next == nullptr
                || scan->next->type != MemoryType::Usable || scan->length < PageSize)
                continue;

            //we found a reclaimable entry followed by a usable one, move a page from usable -> reclaimable
            const uintptr_t found = scan->base + scan->length;
            scan->length += PageSize;
            scan->next->length -= PageSize;
            scan->next->base += PageSize;
            return found;
        }

        if (firstUsable == nullptr || firstUsable->length < PageSize)
            return 0; //abort, no memory for us

        //no nicely arranged regions found, make one
        const uintptr_t found = firstUsable->base;
        ReservePhysicalRange(firstUsable->base, firstUsable->base + PageSize, MemoryType::Reclaimable);
        return found;
    }

    static void MapPage(uintptr_t vaddr, uintptr_t paddr, bool write)
    {
        const size_t pml3Idx = (vaddr >> 25) & 0x7F;
        const size_t pml2Idx = (vaddr >> 18) & 0x7F;
        const size_t pml1Idx = (vaddr >> 12) & 0x3F;

        PageTable* pml3 = reinterpret_cast<PageTable*>(ptRoot);
        PageTable* pml2 = nullptr;
        if ((pml3->entries[pml3Idx] & PteResident) == 0)
        {
            uintptr_t page = AllocPage();
            if (page == 0)
                Panic(PanicReason::InternalAllocFailure);
            sl::memset(sl::NativePtr(page).ptr, 0, sizeof(PageTable));

            page |= PteResident;
            pml3->entries[pml3Idx] = page;
        }
        pml2 = reinterpret_cast<PageTable*>(pml3->entries[pml3Idx] & ~0x1FF);

        PageTable* pml1 = nullptr;
        if ((pml2->entries[pml2Idx] & PteResident) == 0)
        {
            uintptr_t page = AllocPage();
            if (page == 0)
                Panic(PanicReason::InternalAllocFailure);
            sl::memset(sl::NativePtr(page).ptr, 0, sizeof(PageTable));

            page |= PteResident;
            pml2->entries[pml2Idx] = page;
        }
        pml1 = reinterpret_cast<PageTable*>(pml2->entries[pml2Idx] & ~0xFF);

        uint32_t* pte = &pml1->entries[pml1Idx];
        *pte = paddr | PteResident | PteSupervisor | PteCopyback;
        if (!write)
            *pte |= PteWrite;
    }

    void InitMemoryManager()
    {
        freeHeadersUsed = 0;

        //create a map of physical memory
        sl::CNativePtr chunkScan = FindBootInfoTag(BootInfoType::MemChunk);
        while (chunkScan.ptr != nullptr)
        {
            auto chunk = chunkScan.Offset(sizeof(BootInfoTag)).As<BootInfoMemChunk>();
            uintptr_t base = chunk->addr;
            size_t length = chunk->size;

            if (base == 0)
            {
                base += PageSize;
                length -= sl::Min(length, PageSize);
            }

            if (length != 0)
            {
                MemoryBlock* header = AllocMemoryBlock();
                header->type = MemoryType::Usable;
                header->base = base;
                header->length = length;
                physList.PushBack(header);
            }

            chunkScan = chunkScan.Offset(chunkScan.As<BootInfoTag>()->size);
            chunkScan = FindBootInfoTag(BootInfoType::MemChunk, chunkScan);
        }

        const uintptr_t loaderBase = (uintptr_t)LOADER_BLOB_BEGIN;
        const uintptr_t loaderTop = (uintptr_t)LOADER_BLOB_END;
        ReservePhysicalRange(loaderBase, loaderTop, MemoryType::Reclaimable);

        if (auto initrdPtr = FindBootInfoTag(BootInfoType::InitRd); initrdPtr.ptr != nullptr)
        {
            auto desc = initrdPtr.Offset(sizeof(BootInfoTag)).As<BootInfoMemChunk>();
            ReservePhysicalRange(desc->addr, desc->addr + desc->size, MemoryType::KernelModules);
        }

        ptRoot = AllocPage();
        if (ptRoot == 0)
            Panic(PanicReason::InternalAllocFailure);
        sl::memset(reinterpret_cast<void*>(ptRoot), 0, sizeof(PageTable));
    }

    void EnableMmu()
    {
        asm("movec %0, %%srp" :: "d"(ptRoot));
        asm("movec %0, %%dtt0" :: "d"(0)); //we dont use the transparent translation regs
        asm("movec %0, %%dtt1" :: "d"(0)); //so clear them, make sure they dont interfere.
        asm("movec %0, %%itt0" :: "d"(0));
        asm("movec %0, %%itt1" :: "d"(0));

        asm("movec %0, %%tcr" :: "d"(1 << 15)); //set bit 15 to enable translation
    }
    
    size_t HhdmLimit()
    {
        return physList.Back().base + physList.Back().length;
    }

    void* MapMemory(size_t length, uintptr_t vaddr, uintptr_t paddr)
    {
        length = sl::AlignUp(length, PageSize);

        const size_t addend = vaddr % PageSize;
        vaddr = sl::AlignDown(vaddr, PageSize);

        for (size_t i = 0; i < length; i += PageSize)
        {
            const uintptr_t mappedPaddr = paddr == 0 ? AllocPage() : paddr + i;
            if (mappedPaddr == 0)
                return nullptr;

            MapPage(vaddr + i, mappedPaddr, true);
        }

        return reinterpret_cast<void*>(vaddr + addend);
    }

    uintptr_t GetMap(uintptr_t vaddr)
    {
        uint32_t mmusr = 0;
        asm("ptestw (%1); movec %%mmusr, %0" : "=d"(mmusr) : "a"(vaddr));

        if (mmusr & PteResident)
            return (mmusr & ~0xFFF) | (vaddr & 0xFFF);
        return 0;
    }
}
