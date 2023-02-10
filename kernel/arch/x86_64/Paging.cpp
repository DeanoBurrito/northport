#include <arch/Paging.h>
#include <arch/Platform.h>
#include <arch/Cpu.h>
#include <memory/Pmm.h>
#include <debug/Log.h>
#include <stddef.h>
#include <Memory.h>

namespace Npk
{
    constexpr uint64_t PresentFlag = 0b1;
    
    struct PageTable
    {
        uint64_t entries[512];
    };
    
    size_t pagingLevels;
    size_t physAddrMask;
    size_t maxTranslationLevel;
    bool nxSupported;
    bool pgeSupported;

    void* kernelMasterTables;
    sl::Atomic<uint32_t> kernelTablesGen;

    void PagingSetup()
    {
        constexpr uint64_t Cr4_La57 = 1 << 12;
        
        const uint64_t cr4 = ReadCr4();
        pagingLevels = (cr4 & Cr4_La57) ? 5 : 4;
        maxTranslationLevel = (size_t)MaxSupportedPagingSize();
        nxSupported = CpuHasFeature(CpuFeature::NoExecute);
        pgeSupported = CpuHasFeature(CpuFeature::GlobalPages);

        //determine bits needed ot extract a physical address from a PTE.
        physAddrMask = 1ul << (9 * pagingLevels + 12);
        physAddrMask--;
        physAddrMask &= ~(0xFFFul);

        //create the master set of pages used for the higher half.
        kernelMasterTables = (void*)PMM::Global().Alloc();
        sl::memset(AddHhdm(kernelMasterTables), 0, PageSize);
        kernelTablesGen = 0;

        Log("Paging constraints: levels=%lu, maxTranslationLevel=%lu, nxSupport=%s", LogLevel::Info,
            pagingLevels, maxTranslationLevel, nxSupported ? "yes" : "no");
    }

    void* InitPageTables(uint32_t* gen)
    {
        void* ptRoot = reinterpret_cast<void*>(PMM::Global().Alloc());
        sl::memset(AddHhdm(ptRoot), 0, PageSize);

        *gen = kernelTablesGen;
        SyncKernelTables(ptRoot);
        return ptRoot;
    }

    inline void GetPageIndices(uintptr_t virtAddr, size_t* indices)
    {
        if (pagingLevels > 4)
            indices[5] = (virtAddr >> 48) & 0x1FF;
        indices[4] = (virtAddr >> 39) & 0x1FF;
        indices[3] = (virtAddr >> 30) & 0x1FF;
        indices[2] = (virtAddr >> 21) & 0x1FF;
        indices[1] = (virtAddr >> 12) & 0x1FF;
    }

    bool MapMemory(void* root, uintptr_t virt, uintptr_t phys, PageFlags flags, PageSizes size, bool flushEntry)
    {
        if ((size_t)size > maxTranslationLevel)
            return false;
        
        size_t indices[pagingLevels + 1];
        GetPageIndices(virt, indices);

        uint64_t* entry = nullptr;
        PageTable* pt = static_cast<PageTable*>(AddHhdm(root));
        for (size_t i = pagingLevels; i > 0; i--)
        {
            entry = &pt->entries[indices[i]];
            if (i == (size_t)size)
                break;
            
            if (!(*entry & PresentFlag))
            {
                const uintptr_t addr = PMM::Global().Alloc();
                *entry = physAddrMask & addr;
                *entry |= PresentFlag | (uintptr_t)PageFlags::Write;
                sl::memset((void*)AddHhdm(addr), 0, PageSize);
            }

            pt = reinterpret_cast<PageTable*>((*entry & physAddrMask) + hhdmBase);
        }

        //set size bit if needed, or ensure its cleared if not.
        uint64_t actualFlags = ((uint64_t)flags & 0xFFF) | 1; //ensure present is always set
        if (size != PageSizes::_4K)
            actualFlags |= 1 << 7;

        //execute is backwards on x86: if we dont specify the execute flag, set nx
        if (nxSupported && (((uintptr_t)flags >> 63) & 1) == 0)
            actualFlags |= 1ul << 63;
        //force global-flag bit clear if cpu doesn't support the feature.
        if (!pgeSupported)
            actualFlags &= ~(uintptr_t)(1 << 5);
        
        *entry = actualFlags | (phys & physAddrMask);
        if (flushEntry)
            asm volatile("invlpg %0" :: "m"(virt) : "memory");
        return true;
    }

    bool UnmapMemory(void* root, uintptr_t virt, uintptr_t& phys, PageSizes& size, bool flushEntry)
    {
        size_t indices[pagingLevels + 1];
        GetPageIndices(virt, indices);

        uint64_t* entry = nullptr;
        PageTable* pt = static_cast<PageTable*>(AddHhdm(root));
        for (size_t i = pagingLevels; i > 0; i--)
        {
            entry = &pt->entries[indices[i]];
            if ((*entry & PresentFlag) == 0)
                return false;

            if ((i > 1 && *entry & (1 << 7)) || i == 1)
            {
                size = (PageSizes)i;
                break;
            }
            
            pt = reinterpret_cast<PageTable*>((*entry & physAddrMask) + hhdmBase);
        }

        phys = *entry & physAddrMask;
        *entry = 0;

        if (flushEntry)
            asm volatile("invlpg %0" :: "m"(virt) : "memory");
        return true;
    }

    sl::Opt<uintptr_t> GetPhysicalAddr(void* root, uintptr_t virt)
    {
        size_t indices[pagingLevels + 1];
        GetPageIndices(virt, indices);

        uintptr_t mask = 1;
        
        uint64_t* entry = nullptr;
        PageTable* pt = static_cast<PageTable*>(AddHhdm(root));
        for (size_t i = pagingLevels; i > 0; i--)
        {
            entry = &pt->entries[indices[i]];

            if ((*entry & PresentFlag) == 0)
                return {};

            if ((i > 1 && *entry & (1 << 7)) || i == 1)
            {
                mask <<= ((i - 1) * 9 + 12);
                mask--;
                break;
            }
            
            pt = reinterpret_cast<PageTable*>((*entry & physAddrMask) + hhdmBase);
        }

        return (*entry & ~(mask | 1ul << 63)) | (virt & mask);
    }

    void SyncKernelTables(void *dest)
    {
        const PageTable* masterTables = AddHhdm(static_cast<PageTable*>(kernelMasterTables));
        PageTable* destTables = AddHhdm(static_cast<PageTable*>(dest));

        for (size_t i = 256; i < 512; i++)
            destTables->entries[i] = masterTables->entries[i];
    }

    void LoadTables(void* root)
    {
        asm volatile("mov %0, %%cr3" :: "r"((uint64_t)root) : "memory");
    }

    PageSizes MaxSupportedPagingSize()
    {
        return CpuHasFeature(CpuFeature::Pml3Translation) ? PageSizes::_1G : PageSizes::_2M;
    }

    extern "C" void HatHandlePanic()
    {
        LoadTables(kernelMasterTables);
    }
}
