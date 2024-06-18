#include "Memory.h"
#include "Util.h"
#include <NativePtr.h>
#include <Maths.h>
#include <containers/List.h>
#include <Memory.h>
#include <boot/Limine.h>

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

    static void MapPage(uintptr_t vaddr, uintptr_t paddr, bool write)
    {
        const size_t pml3Idx = (vaddr >> 25) & 0x7F;
        const size_t pml2Idx = (vaddr >> 18) & 0x7F;
        const size_t pml1Idx = (vaddr >> 12) & 0x3F;

        PageTable* pml3 = reinterpret_cast<PageTable*>(ptRoot);
        PageTable* pml2 = nullptr;
        if ((pml3->entries[pml3Idx] & PteResident) == 0)
        {
            uintptr_t page = AllocPages(1);
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
            uintptr_t page = AllocPages(1);
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

        ptRoot = AllocPages(1);
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

    size_t GenerateLbpMemoryMap(void* store, size_t count)
    {
        size_t accum = 0;
        for (auto it = physList.Begin(); it != physList.End(); it = it->next)
            accum++;

        if (store == nullptr || accum > count)
            return accum;

        auto entries = static_cast<limine_memmap_entry*>(store);
        for (auto it = physList.Begin(); it != physList.End(); it = it->next)
        {
            limine_memmap_entry* entry = entries++;
            entry->base = it->base;
            entry->length = it->length;

            switch (it->type)
            {
            case MemoryType::Usable: 
                entry->type = LIMINE_MEMMAP_USABLE;
                break;
            case MemoryType::Reclaimable: 
                entry->type = LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE;
                break;
            case MemoryType::KernelModules:
                entry->type = LIMINE_MEMMAP_KERNEL_AND_MODULES;
                break;
            }
        }

        return accum;
    }

    uintptr_t AllocPages(size_t count, MemoryType type)
    {
        const size_t length = count * PageSize;

        MemoryBlock* firstUsable = nullptr;
        for (MemoryBlock* scan = physList.Begin(); scan != nullptr; scan = scan->next)
        {
            if (firstUsable == nullptr && scan->type == MemoryType::Usable)
                firstUsable = scan;
            if (type != MemoryType::Reclaimable)
                continue;

            if (scan->type != MemoryType::Reclaimable || scan->next == nullptr
                || scan->next->type != MemoryType::Usable || scan->length < length)
                continue;

            //we found a pair of regions ([reclaimable][usable]), move memory from usable to reclaimable
            const uintptr_t found = scan->base + scan->length;
            scan->length += length;
            scan->next->length -= length;
            scan->next->base += length;
            return found;
        }

        if (firstUsable == nullptr || firstUsable->length < length)
            return 0; //abort, no memory for us

        //no nicely arranged regions found, make one
        const uintptr_t found = firstUsable->base;
        ReservePhysicalRange(firstUsable->base, firstUsable->base + length, type);
        return found;
    }

    void* AllocGeneral(size_t size)
    {
        const size_t pages = sl::AlignUp(size, PageSize);
        const uintptr_t found = AllocPages(pages); //TODO: we can do better than this surely

        if (found == 0)
            return nullptr;
        return reinterpret_cast<void*>(found + HhdmBase);
    }

    void* MapMemory(size_t length, uintptr_t vaddr, uintptr_t paddr)
    {
        const uintptr_t top = sl::AlignUp(vaddr + length, PageSize);
        const size_t addend = vaddr % PageSize;
        vaddr = sl::AlignDown(vaddr, PageSize);
        length = top - vaddr;

        for (size_t i = 0; i < length; i += PageSize)
        {
            const uintptr_t mappedPaddr = (paddr == 0 ? AllocPages(1) : paddr + i);
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

void* operator new(size_t size)
{
    return Npl::AllocGeneral(size);
}

void* operator new[](size_t size)
{
    return Npl::AllocGeneral(size);
}

void operator delete(void*) noexcept
{
    Panic(Npl::PanicReason::DeleteCalled);
}

void operator delete(void*, size_t) noexcept
{
    Panic(Npl::PanicReason::DeleteCalled);
}

void operator delete[](void*) noexcept
{
    Panic(Npl::PanicReason::DeleteCalled);
}

void operator delete[](void*, size_t) noexcept
{
    Panic(Npl::PanicReason::DeleteCalled);
}
