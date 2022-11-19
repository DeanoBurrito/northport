#include <arch/Paging.h>
#include <arch/Platform.h>
#include <debug/Log.h>
#include <memory/Pmm.h>
#include <Memory.h>

namespace Npk
{
    constexpr uint64_t ValidFlag = 1 << 0;
    constexpr uint64_t ReadFlag = 1 << 1;

    struct PageTable
    {
        uint64_t entries[512];
    };

    uintptr_t satpBits;
    size_t pagingLevels;
    size_t physAddrMask;
    size_t maxTranslationLevel;

    void* kernelMasterTables;
    uint32_t kernelTablesGen;

    void PagingSetup()
    {
        satpBits = ReadCsr("satp") & (0xFul << 60);
        pagingLevels = (satpBits >> 60) - 5;
        maxTranslationLevel = pagingLevels;

        physAddrMask = 1ul << (9 * pagingLevels + 12);
        physAddrMask--;
        physAddrMask &= ~0x3FFul;

        kernelMasterTables = (void*)PMM::Global().Alloc();
        sl::memset(AddHhdm(kernelMasterTables), 0, PageSize);
        kernelTablesGen = 0;

        Log("Paging constraints: levels=%lu, maxTranslationLevel=%lu.", LogLevel::Info,
            pagingLevels, maxTranslationLevel);
    }

    void* InitPageTables(uint32_t* gen)
    {
        void* ptRoot = reinterpret_cast<void*>(PMM::Global().Alloc());
        sl::memset(AddHhdm(ptRoot), 0, PageSize);

        *gen = __atomic_load_n(&kernelTablesGen, __ATOMIC_ACQUIRE);
        SyncKernelTables(ptRoot);
        return ptRoot;
    }

    inline void GetPageindices(uintptr_t virtAddr, size_t* indices)
    {
        if (pagingLevels > 4)
            indices[5] = (virtAddr >> 48) & 0x1FF;
        if (pagingLevels > 3)
            indices[4] = (virtAddr >> 39) & 0x1FF;
        indices[3] = (virtAddr >> 30) & 0x1FF;
        indices[2] = (virtAddr >> 21) & 0x1FF;
        indices[1] = (virtAddr >> 12) & 0x1FF;
    }

    bool MapMemory(void* root, uintptr_t vaddr, uintptr_t paddr, PageFlags flags, PageSizes size, bool flushEntry)
    {
        if (size > maxTranslationLevel)
            return false;
        
        size_t indices[pagingLevels + 1];
        GetPageindices(vaddr, indices);

        uint64_t* entry = nullptr;
        PageTable* pt = static_cast<PageTable*>(AddHhdm(root));
        for (size_t i = pagingLevels; i > 0; i--)
        {
            entry = &pt->entries[indices[i]];
            if (i == size)
                break;
            
            if (!(*entry & ValidFlag))
            {
                uintptr_t addr = PMM::Global().Alloc();
                *entry = (addr >> 2) & physAddrMask;
                *entry |= ValidFlag;
                sl::memset(reinterpret_cast<void*>(AddHhdm(addr)), 0, PageSize);
            }

            pt = reinterpret_cast<PageTable*>(((*entry & physAddrMask) << 2) + hhdmBase);
        }

        *entry = (paddr >> 2) & physAddrMask;
        *entry |= ValidFlag | ReadFlag | ((uintptr_t)flags & 0x3FF); //valid + read are always set.
        if (flushEntry)
            asm volatile("sfence.vma %0, zero" :: "r"(vaddr) : "memory");
        return true;
    }

    bool UnmapMemory(void* root, uintptr_t vaddr, uintptr_t& paddr, PageSizes& size, bool flushEntry)
    {
        size_t indices[pagingLevels + 1];
        GetPageindices(vaddr, indices);

        uint64_t* entry = nullptr;
        PageTable* pt = static_cast<PageTable*>(AddHhdm(root));
        for (size_t i = pagingLevels; i > 0; i--)
        {
            entry = &pt->entries[indices[i]];
            if ((*entry & ValidFlag) == 0)
                return false;
            
            if (*entry & 0b1110)
            {
                size = (PageSizes)i;
                break;
            }

            pt = reinterpret_cast<PageTable*>(((*entry & physAddrMask) << 2) + hhdmBase);
        }

        paddr = (*entry << 2) & physAddrMask;
        *entry = 0;

        if (flushEntry)
            asm volatile("sfence.vma %0, zero" :: "r"(vaddr) : "memory");
        return true;
    }

    sl::Opt<uintptr_t> GetPhysicalAddr(void* root, uintptr_t virt)
    {
        size_t indices[pagingLevels + 1];
        GetPageindices(virt, indices);

        uintptr_t mask = 1;

        uint64_t* entry = nullptr;
        PageTable* pt = static_cast<PageTable*>(AddHhdm(root));
        for (size_t i = pagingLevels; i > 0; i--)
        {
            entry = &pt->entries[indices[i]];
            if ((*entry & ValidFlag) == 0)
                return {};
            
            if (*entry & 0b1110)
            {
                mask <<= i * 9 + 12;
                mask--;
                break;
            }

            pt = reinterpret_cast<PageTable*>(((*entry & physAddrMask) << 2) + hhdmBase);
        }

        return ((*entry << 2) & ~mask) | (virt & mask);
    }

    void SyncKernelTables(void* dest)
    {
        const PageTable* masterTables = AddHhdm(static_cast<PageTable*>(kernelMasterTables));
        PageTable* destTables = AddHhdm(static_cast<PageTable*>(dest));

        for (size_t i = 0; i < 256; i++)
            destTables->entries[i] = masterTables->entries[i];
    }

    void LoadTables(void* root)
    {
        WriteCsr("satp", satpBits | ((uintptr_t)root >> 12));
        //this sfence flushes all non-global tlb entries
        asm volatile("sfence.vma zero, %0" :: "r"(0) : "memory");
    }

    size_t GetHhdmLimit()
    {
        return pagingLevels == 3 ? 8 * GiB : TiB / 2;
    }

    PageSizes MaxSupportedPagingSize()
    {
        return (PageSizes)maxTranslationLevel;
    }
}
