#include <private/Hardware.hpp>
#include <hardware/common/mmu/PageTables.hpp>
#include <hardware/x86_64/Cpuid.hpp>
#include <hardware/x86_64/Private.hpp>
#include <private/Core.hpp>
#include <private/Entry.hpp>
#include <Core.hpp>
#include <lib/Maths.hpp>
#include <lib/Memory.hpp>

#define COPY_PTE(dest_ptr, src_ptr) \
    do \
    { \
        asm volatile("movq (%1), %%rcx; movq %%rcx, (%0)" :: \
            "r"(dest_ptr), "r"(src_ptr) : "memory", "rcx"); \
    } while (false)

#define INVLPG(vaddr) \
    do \
    { \
        asm volatile("invlpg (%0)" :: "r"(vaddr) : "memory"); \
    } while (false)

namespace Npk
{
    constexpr uint64_t Cr4Pge = 1 << 7;
    constexpr uint64_t Cr4La57 = 1 << 12;
    constexpr size_t PtEntries = 512;

    constexpr uint64_t PresentBit = 1 << 0;
    constexpr uint64_t WriteBit = 1 << 1;
    constexpr uint64_t UserBit = 1 << 2;
    constexpr uint64_t AccessedBit = 1 << 5;
    constexpr uint64_t DirtyBit = 1 << 6;
    constexpr uint64_t BigPageBit = 1 << 7;
    constexpr uint64_t GlobalBit = 1 << 8;
    constexpr uint64_t NxBit = 1ull << 63;

    //NOTE: this only works on 4K pages.
    constexpr uint64_t CacheMask = (1 << 3) | (1 << 4) | (1 << 7);
    //NOTE: that pat indices assume the PAT layout defined by limine protocol.
    constexpr uint64_t PatUcBits = (1 << 4) | (1 << 3);
    constexpr uint64_t PatWcBits = (1 << 7) | (1 << 3);
    constexpr uint64_t LegacyUcBits = (1 << 4) | (1 << 3);

    static bool nxSupport;
    static bool patSupport;
    static bool globalPageSupport;
    static uint64_t mmioBits;
    static uint64_t framebufferBits;
    static uint64_t addrMask;
    static size_t ptLevels;

    Paddr kernelRoot;
    Paddr apBootPage;

    static uintptr_t tempMapBase;
    static sl::Span<uint64_t> tempMapAccess;

    constexpr uint8_t PtLevelShifts[] = 
    { 
        12, 
        21, 
        30, 
        39, 
        48
    };

    constexpr uintptr_t PtLevelMasks[] = 
    { 
        0x1FF,
        0x1FF,
        0x1FF,
        0x1FF,
        0x1FF
    };

    constexpr size_t PtLevelSizes[] = 
    {
        0x1000, 
        0x1000, 
        0x1000, 
        0x1000, 
        0x1000
    };

    static PageTableConfig ptConf
    {
        .levelCount = 0, //set by `HwInitBspMmu()`.
        .levelShift = PtLevelShifts,
        .levelMask = PtLevelMasks,
        .levelPtSize = PtLevelSizes,
        .leafLevelMask = 0x1FF,
        .pteSize = 8,
        .kernelFirstIndex = 256,
        .kernelLastIndex = 511,
        .splitRoot = false,
        .hwAccessedBit = true,
        .hwDirtyBit = true,
        .hasCustomWritePte = false,
        .hasCustomExchange = false,
        .hasCustomCompareExchange = false,
    };

    const PageTableConfig& GetPageTableConfig()
    {
        return ptConf;
    }

    void SetUserRoot(Paddr root, Asid asid)
    {
        (void)asid; //TODO: detect pcid support

        WRITE_CR(3, root);
    }

    Paddr GetUserRoot()
    {
        const uint64_t value = READ_CR(3);

        return value & addrMask;
    }

    void MakeLeafPte(void* pte, Paddr paddr, MmuPermissions perms, 
        MmuCacheMode cacheMode, bool kernel, size_t level)
    {
        uint64_t value = (paddr & addrMask) | PresentBit;
        if (perms.Has(MmuPermission::Write))
            value |= WriteBit;

        if (!kernel)
            value |= UserBit;
        else
            value |= GlobalBit;

        if (nxSupport && !perms.Has(MmuPermission::Fetch))
            value |= NxBit;
        if (level != 0)
            value |= BigPageBit;

        switch (cacheMode)
        {
        case MmuCacheMode::Mmio:
            value |= mmioBits;
            break;
        
        case MmuCacheMode::Framebuffer:
            value |= framebufferBits;
            break;

        default:
            break;
        }

        COPY_PTE(pte, &value);
    }

    void MakeIntermediatePte(void* pte, Paddr child, bool kernel)
    {
        uint64_t value = (child & addrMask) | PresentBit | WriteBit;

        if (!kernel)
            value |= UserBit;
        else
            value |= GlobalBit;

        COPY_PTE(pte, &value);
    }

    void MakeInvalidPte(void* pte)
    {
        uint64_t value = 0;

        COPY_PTE(pte, &value);
    }

    void SetPtePerms(void* pte, MmuPermissions perms)
    {
        uint64_t* ptr = static_cast<uint64_t*>(pte);

        *ptr = *ptr & ~(WriteBit | NxBit);

        if (perms.Has(MmuPermission::Write))
            *ptr |= WriteBit;
        if (nxSupport && !perms.Has(MmuPermission::Fetch))
            *ptr |= NxBit;
    }

    bool IsPteValid(const void* pte)
    {
        const uint64_t value = *static_cast<const uint64_t*>(pte);

        return value & PresentBit;
    }

    bool IsLeafPte(const void* pte, size_t level)
    {
        const uint64_t value = *static_cast<const uint64_t*>(pte);

        if (level == 0)
            return value & PresentBit;
        return value & BigPageBit;
    }

    Paddr GetPteAddr(const void* pte)
    {
        const uint64_t value = *static_cast<const uint64_t*>(pte);

        return value & addrMask;
    }

    MmuPermissions GetPtePerms(const void* pte)
    {
        const uint64_t value = *static_cast<const uint64_t*>(pte);
        MmuPermissions perms {};

        if (value & WriteBit)
            perms.Set(MmuPermission::Write);
        if (!nxSupport || !(value & NxBit))
            perms.Set(MmuPermission::Fetch);

        return perms;
    }

    MmuCacheMode GetPteCacheMode(const void* pte)
    {
        const uint64_t value = *static_cast<const uint64_t*>(pte);

        if (patSupport)
        {
            if ((value & CacheMask) == mmioBits)
                return MmuCacheMode::Mmio;
            if ((value & CacheMask) == framebufferBits)
                return MmuCacheMode::Framebuffer;

            return MmuCacheMode::Default;
        }

        if ((value & LegacyUcBits) == LegacyUcBits)
            return MmuCacheMode::Mmio;

        return MmuCacheMode::Default;
    }

    bool IsPteAccessed(const void* pte)
    {
        const uint64_t value = *static_cast<const uint64_t*>(pte);

        return value & AccessedBit;
    }

    bool IsPteDirty(const void* pte)
    {
        const uint64_t value = *static_cast<const uint64_t*>(pte);

        return value & DirtyBit;
    }

    void WritePte(void* dest, const void* source)
    {
        (void)dest;
        (void)source;

        NPK_UNREACHABLE();
    }

    void ExchangePte(void* dest, const void* source, void* prev)
    {
        (void)dest;
        (void)source;
        (void)prev;

        NPK_UNREACHABLE();
    }

    bool CompareExchangePte(void* dest, void* expected, const void* desired)
    {
        (void)dest;
        (void)expected;
        (void)desired;

        NPK_UNREACHABLE();
    }

    bool ClearPteAccessed(void* pte)
    {
        auto* ptr = static_cast<sl::Atomic<uint64_t>*>(pte);

        return ptr->FetchAnd(~AccessedBit, sl::SeqCst) & AccessedBit;
    }

    bool ClearPteDirty(void* pte)
    {
        auto* ptr = static_cast<sl::Atomic<uint64_t>*>(pte);

        return ptr->FetchAnd(~DirtyBit, sl::SeqCst) & DirtyBit;
    }

    bool SetPteAccessed(void* pte)
    {
        auto* ptr = static_cast<sl::Atomic<uint64_t>*>(pte);

        return ptr->FetchOr(AccessedBit, sl::SeqCst) & AccessedBit;
    }

    bool SetPteDirty(void* pte)
    {
        auto* ptr = static_cast<sl::Atomic<uint64_t>*>(pte);

        return ptr->FetchOr(DirtyBit, sl::SeqCst) & DirtyBit;
    }

    bool PteIsWriteTrackable(const void* pte)
    {
        (void)pte;

        NPK_UNREACHABLE();
    }

    static Paddr DoEarlyMap(InitState& state, Paddr paddr, uintptr_t vaddr,
        MmuPermissions perms, MmuCacheMode cacheMode)
    {
        size_t indices[6];

        indices[5] = (vaddr >> 48) & 0x1FF;
        indices[4] = (vaddr >> 39) & 0x1FF;
        indices[3] = (vaddr >> 30) & 0x1FF;
        indices[2] = (vaddr >> 21) & 0x1FF;
        indices[1] = (vaddr >> 12) & 0x1FF;

        auto* pt = reinterpret_cast<uint64_t*>(kernelRoot + state.dmBase);

        for (size_t i = ptLevels; i != 1; i--)
        {
            uint64_t* pte = &pt[indices[i]];

            if ((*pte & PresentBit) == 0)
            {
                uint64_t value;
                MakeIntermediatePte(&value, state.PmAlloc(), true);

                COPY_PTE(pte, &value);
            }

            pt = reinterpret_cast<uint64_t*>((*pte & addrMask) + state.dmBase);
        }

        uint64_t leaf;
        MakeLeafPte(&leaf, paddr, perms, cacheMode, true, 0);
        COPY_PTE(&pt[indices[1]], &leaf);

        return reinterpret_cast<uintptr_t>(pt) - state.dmBase;
    }

    uintptr_t HwInitBspMmu(InitState& state, size_t tempMapCount)
    {
        ptLevels = 4;

        const uint64_t cr4 = READ_CR(4);
        if (cr4 & Cr4La57)
            ptLevels = 5;
        ptConf.levelCount = ptLevels;

        nxSupport = CpuHasFeature(CpuFeature::NoExecute);
        patSupport = CpuHasFeature(CpuFeature::Pat);
        globalPageSupport = CpuHasFeature(CpuFeature::GlobalPages);

        if (patSupport)
        {
            mmioBits = PatUcBits;
            framebufferBits = PatWcBits;
        }
        else
        {
            mmioBits = LegacyUcBits;
            framebufferBits = LegacyUcBits;
        }

        addrMask = 1ull << (9 * ptLevels + 12);
        addrMask--;
        addrMask &= ~0xFFFul;

        //TODO: software direct map segments
        //- opt-in via command line flag since this is a huge security risk,
        //make it the user's choice to trae security vs speed.
        //- use larger page sizes where possible
        //- ensure its mapped no-execute
        //- dont make the software map if physical ram is limited, if the
        //direct map would consume too much ram limit it's size or skip it.
        //- ensure kernel image isn't mapped.
        //- ensure only usable ram regions are mapped, otherwise some AMD cpus
        //can fire an MCE
        // - https://github.com/torvalds/linux/commit/66520ebc2df3fe52eb4792f8101fac573b766baf

        //the kernel root has to exist before any early mapping, since that is
        //the table DoEarlyMap() populates.
        kernelRoot = state.PmAlloc();

        //for AP bringup purposes the kernel root PT must be 32-bit addressable.
        NPK_ASSERT(kernelRoot >> 32 == 0);

        apBootPage = state.PmAlloc();
        HwEarlyMap(state, apBootPage, apBootPage,
            MmuPermission::Write | MmuPermission::Fetch, {});

        const size_t blobLength = reinterpret_cast<uintptr_t>(_EndOfSpinupBlob)
            - reinterpret_cast<uintptr_t>(SpinupBlob);
        NPK_ASSERT(blobLength <= PageSize());
        sl::MemCopy(reinterpret_cast<void*>(apBootPage + state.dmBase),
            SpinupBlob, blobLength);
        Log("AP boot blob @ 0x%tx", LogLevel::Verbose, apBootPage);

        state.vmAllocHead = -(1ull << (9 * ptLevels + 11));

        tempMapBase = state.vmAllocHead;
        tempMapCount = sl::AlignUp(tempMapCount, PtEntries);
        state.vmAllocHead += tempMapCount << PfnShift();
        tempMapAccess = { reinterpret_cast<uint64_t*>(state.vmAllocHead),
            tempMapCount };

        for (size_t i = 0; i < tempMapCount; i++)
        {
            const Paddr pt = DoEarlyMap(state, 0,
                tempMapBase + (i << PfnShift()), MmuPermission::Write, {});

            if ((i & (PtEntries - 1)) == 0)
            {
                HwEarlyMap(state, pt, state.vmAllocHead, MmuPermission::Write,
                    {});
                state.vmAllocHead += PageSize();
            }
        }
        Log("Temp mappings prepared: 0x%tx (access @ %p, %zu)", LogLevel::Info,
            tempMapBase, tempMapAccess.Begin(), tempMapAccess.Size());

        sysDomain0.kernelMap = HwCreateKernelMap(state, kernelRoot);

        return state.vmAllocHead;
    }

    void HwCompleteBspMmuInit()
    {
        WRITE_CR(3, kernelRoot);
    }

    void HwEarlyMap(InitState& state, Paddr paddr, uintptr_t vaddr,
        MmuPermissions perms, MmuCacheMode mode)
    {
        DoEarlyMap(state, paddr, vaddr, perms, mode);
    }

    void* HwSetTempMapSlot(size_t index, Paddr paddr)
    {
        if (index >= tempMapAccess.Size())
            return nullptr;

        const uintptr_t vaddr = (index << PfnShift()) + tempMapBase;
        const uint64_t pte = (paddr & addrMask) | PresentBit | WriteBit;

        COPY_PTE(&tempMapAccess[index], &pte);
        INVLPG(vaddr);

        return reinterpret_cast<void*>(vaddr);
    }

    HwAddressRange HwGetUserAddressRange()
    {
        //The only interesting thing here is we keep the highest page of the
        //lower half as unmappable, since there can be bugs with placing a 
        //`syscall` instruction at the end of the last page. On some cpus this
        //can lead to a matching `sysret` faulting in the kernel while trying to
        //return to userspace, allowing user mode software to trigger a kernel
        //mode fault.
        const uintptr_t Max = (1ull << (9 * ptLevels + 11)) - PageSize();

        //for > 32-bit address spaces mark the lower 4G as unusable by userspace
        //to help catch any badly typed casts (pointers as `int`s etc). Not a
        //real constraint, but an easy way to catch this type of bug.
        //On all address spaces we prevent the lowest page (PFN 0) from being
        //valid, to catch null pointer usage.
        if constexpr (sizeof(uintptr_t) == 4)
            return { 4 * KiB, Max };
        else
            return { 4 * GiB, Max };
    }

    sl::Span<HwAddressRange> HwGetKernelAddressRanges()
    {
        //TODO:
        return {};
    }

    sl::Span<const HwDirectMapSegment> HwGetDirectMapSegments()
    {
        return {};
    }

    void HwFlushTlb(Asid asid, uintptr_t base, size_t length)
    {
        //TODO: once PCIDs are in we'll need the code below for the case of
        //AsidNone being passed as `asid`, meaning it must affect all spaces.
        //Otherwise we can use invlpcid (type 0) to save some parts of the tlb
        //(hopefully).
        (void)asid;

        const uintptr_t top = base + length;
        base = AlignDownPage(base);

        while (base < top)
        {
            INVLPG(base);
            base += PageSize();
        }
    }

    void HwFlushTlbAll(Asid asid)
    {
        (void)asid;

        if (globalPageSupport)
        {
            const uint64_t cr4 = READ_CR(4);
            WRITE_CR(4, cr4 & ~Cr4Pge);
            WRITE_CR(4, cr4);

            return;
        }

        const uint64_t prev = READ_CR(3);
        WRITE_CR(3, prev);
    }

    bool HwHasBroadcastInvalidate()
    {
        //TODO: invlpgb + tlbsync support, <3 AMD.
        return false;
    }

    size_t HwGetTlbFlushThreshold()
    {
        //TODO: detremine from cpuid, the value below is copied from managarm
        //as a starter.

        return 64;
    }
}
