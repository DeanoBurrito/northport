#include <KernelApi.hpp>
#include <AcpiTypes.hpp>
#include <Scheduler.hpp>
#include <BakedConstants.hpp>
#include <interfaces/loader/Generic.hpp>
#include <Maths.h>
#include <Memory.h>
#include <UnitConverter.h>

#include <hardware/Entry.hpp>

namespace Npk
{
    void DispatchIpi() {}
    void DispatchInterrupt(size_t vector) { (void)vector; }
    void DispatchPageFault(PageFaultFrame* frame) { (void)frame; }
    void DispatchSyscall(SyscallFrame* frame) { (void)frame; }
    void DispatchException(ExceptionFrame* frame) { (void)frame; }

    static sl::Opt<Paddr> rootPtrs[static_cast<size_t>(ConfigRootType::Count)];

    sl::Opt<Paddr> GetConfigRoot(ConfigRootType type)
    {
        const size_t index = static_cast<size_t>(type);
        if (index < static_cast<size_t>(ConfigRootType::Count))
            return rootPtrs[index];
        return {};
    }

    MemoryDomain domain0;

    char* InitState::VmAllocAnon(size_t length)
    {
        char* base = VmAlloc(length);
        for (size_t i = 0; i < length; i += PageSize())
            ArchEarlyMap(*this, PmAlloc(), (uintptr_t)base + i, MmuFlag::Write);

        return base;
    }

    Paddr InitState::PmAlloc()
    {
        Loader::MemoryRange range;

        while (true)
        {
            const size_t count = Loader::GetUsableRanges({ &range, 1 }, pmAllocIndex);
            NPK_ASSERT(count != 0);

            pmAllocHead = sl::Max(pmAllocHead, range.base);
            if (pmAllocHead + PageSize() > range.base + range.length)
            {
                pmAllocIndex++;
                continue;
            }

            usedPages++;
            const Paddr ret = pmAllocHead;
            pmAllocHead += PageSize();

            sl::MemSet(reinterpret_cast<void*>(dmBase + ret), 0, PageSize());
            return ret;
        }
        NPK_UNREACHABLE();
    }

    static void InitPageInfoStore(InitState& state)
    {
        constexpr size_t MaxLoaderRanges = 32;

        Loader::MemoryRange ranges[MaxLoaderRanges];
        Paddr minUsablePaddr = static_cast<Paddr>(~0);
        Paddr maxUsablePaddr = 0;
        size_t rangesBase = 0;

        //determine highest page index we'll need, so we can reserve virtual address space
        while (true)
        {
            const size_t count = Loader::GetUsableRanges(ranges, rangesBase);
            rangesBase += count;

            for (size_t i = 0; i < count; i++)
            {
                maxUsablePaddr = sl::Max(maxUsablePaddr, ranges[i].base + ranges[i].length);
                minUsablePaddr = sl::Min(minUsablePaddr, ranges[i].base);
            }

            if (count < MaxLoaderRanges)
                break;
        }
        
        const size_t dbSize = ((maxUsablePaddr - minUsablePaddr) >> PfnShift()) * sizeof(PageInfo);
        domain0.physOffset = minUsablePaddr;
        domain0.pfndb = reinterpret_cast<PageInfo*>(state.VmAlloc(dbSize));

        //sparsely map the storage space
        const uintptr_t virtOffset = reinterpret_cast<uintptr_t>(domain0.pfndb);
        rangesBase = 0;
        while (true)
        {
            const size_t count = Loader::GetUsableRanges(ranges, rangesBase);
            rangesBase += count;

            for (size_t i = 0; i < count; i++)
            {
                Paddr base = ranges[i].base - domain0.physOffset;
                Paddr top = base + ranges[i].length;

                base = AlignDownPage((base >> PfnShift()) * sizeof(PageInfo));
                top = AlignUpPage((top >> PfnShift()) * sizeof(PageInfo));
                Log("PageInfo region: 0x%tx-0x%tx -> 0x%tx-0x%tx", LogLevel::Trace,
                    ranges[i].base, ranges[i].base + ranges[i].length, base, top);

                for (Paddr s = base; s < top; s += PageSize())
                    ArchEarlyMap(state, state.PmAlloc(), virtOffset + s, MmuFlag::Write);
            }

            if (count < MaxLoaderRanges)
                break;
        }

        Log("PageInfo store mapped: %zu ranges covering 0x%tx-0x%tx", LogLevel::Info,
            rangesBase, minUsablePaddr, maxUsablePaddr);
    }

    static void MapKernelImage(InitState& state, Paddr physBase)
    {
        const uintptr_t virtBase = reinterpret_cast<uintptr_t>(KERNEL_BLOB_BEGIN);

        for (char* i = AlignDownPage(KERNEL_TEXT_BEGIN); i < KERNEL_TEXT_END; i += PageSize())
            ArchEarlyMap(state, (Paddr)i - virtBase + physBase, (uintptr_t)i, MmuFlag::Fetch);

        for (char* i = AlignDownPage(KERNEL_RODATA_BEGIN); i < KERNEL_RODATA_END; i += PageSize())
            ArchEarlyMap(state, (Paddr)i - virtBase + physBase, (uintptr_t)i, {});

        for (char* i = AlignDownPage(KERNEL_DATA_BEGIN); i < KERNEL_DATA_END; i += PageSize())
            ArchEarlyMap(state, (Paddr)i - virtBase + physBase, (uintptr_t)i, MmuFlag::Write);

        Log("Kernel image mapped: vbase=%p, pbase=0x%tx", LogLevel::Info, 
            KERNEL_BLOB_BEGIN, physBase);
    }
    
    static uintptr_t InitPerCpuStore(InitState& state, size_t cpuCount)
    {
        const size_t localsSize = reinterpret_cast<uintptr_t>(KERNEL_CPULOCALS_END) 
            - reinterpret_cast<uintptr_t>(KERNEL_CPULOCALS_BEGIN);
        const size_t totalSize = AlignUpPage(cpuCount * localsSize);

        const char* base = state.VmAllocAnon(totalSize);

        const auto conv = sl::ConvertUnits(localsSize);
        Log("Cpu-local stores mapped: %zu.%zu %sB each, based at %p", LogLevel::Info,
            conv.major, conv.minor, conv.prefix, base);
        return reinterpret_cast<uintptr_t>(base);
    }
    
    static uintptr_t InitApStacks(InitState& state, size_t apCount)
    {
        const size_t stride = KernelStackSize() + PageSize();
        const size_t totalSize = apCount * stride;

        const auto base = state.VmAllocAnon(totalSize);

        Log("Idle stacks mapped: 0x%zxB each, based at %p", LogLevel::Info, 
            KernelStackSize(), base);
        return reinterpret_cast<uintptr_t>(base);
    }

    static sl::StringSpan CopyCommandLine(InitState& state, sl::StringSpan source)
    {
        char* vbase = state.VmAlloc(source.Size());
        for (size_t i = 0; i < source.Size(); i += PageSize())
        {
            const Paddr page = state.PmAlloc();
            const size_t len = sl::Min(source.Size() - i, PageSize());
            sl::MemCopy(reinterpret_cast<void*>(page + state.dmBase), source.Begin() + i, len);

            ArchEarlyMap(state, page, reinterpret_cast<uintptr_t>(vbase) + i, {});
        }

        return sl::StringSpan(vbase, source.Size());
    }

    static void InitPmFreeList(InitState& state)
    {
        constexpr size_t MaxLoaderRanges = 32;

        Loader::MemoryRange ranges[MaxLoaderRanges];
        size_t rangesBase = state.pmAllocIndex;
        size_t totalPages = 0;

        Log("Populating domain0 PM freelist from bootloader map:", LogLevel::Verbose);
        Log("%9s|%18s|%12s|%12s", LogLevel::Verbose, 
            "New Pages", "Base Address", "Total Pages", "Total Size");

        while (true)
        {
            const size_t count = Loader::GetUsableRanges(ranges, rangesBase);
            rangesBase += count;

            for (size_t i = 0; i < count; i++)
            {
                const Paddr top = ranges[i].base + ranges[i].length;
                const Paddr base = sl::Max(ranges[i].base, state.pmAllocHead);
                const size_t pageCount = (top - base) >> PfnShift();

                PageInfo* info = LookupPageInfo(base);
                totalPages += pageCount;

                const auto conv = sl::ConvertUnits(totalPages << PfnShift());
                Log("%9zu|%#18tx|%12zu|%4zu.%03zu %sB", LogLevel::Verbose,
                    pageCount, base, totalPages, conv.major, conv.minor, conv.prefix);

                const KernelMap loaderMap = ArchSetKernelMap({});
                info->pm.count = pageCount;
                domain0.freeLists.free.PushBack(info);
                domain0.freeLists.pageCount += pageCount;
                ArchSetKernelMap(loaderMap);
            }

            if (count < MaxLoaderRanges)
                break;
        }

        const auto conv = sl::ConvertUnits(totalPages << PfnShift());
        const auto kernConv = sl::ConvertUnits(state.usedPages << PfnShift());
        Log("%zu.%zu %sB usable memory, kernel init used %zu.%zu %sB", LogLevel::Info, conv.major, 
            conv.minor, conv.prefix, kernConv.major, kernConv.minor, kernConv.prefix);
    }

    static void PrintWelcome()
    {
        Log("Northport kernel v%zu.%zu.%zu starting ...", LogLevel::Info,
            versionMajor, versionMinor, versionRev);
        Log("Compiler flags: %s", LogLevel::Verbose, compileFlags);
        Log("Base Commit%s: %s", LogLevel::Verbose, gitDirty ? " (dirty)" : "",
            gitHash);
    }

    struct SetupInfo
    {
        uintptr_t virtBase;
        uintptr_t apStacks;
        size_t stackStride;
        uintptr_t perCpuStores;
        size_t perCpuStride;
        sl::StringSpan configCopy;
        size_t pmaEntries;
        uintptr_t pmaSlots;
    };

    static SetupInfo SetupDomain0(const Loader::LoadState& loaderState)
    {
        SetupInfo setupInfo {};
        setupInfo.stackStride = KernelStackSize() + PageSize();
        setupInfo.pmaEntries = ReadConfigUint("npk.pm.temp_mapping_count", 512);

        InitState initState {};
        initState.dmBase = loaderState.directMapBase;
        initState.vmAllocHead = ArchInitBspMmu(initState, setupInfo.pmaEntries);

        domain0.zeroPage = initState.PmAlloc();
        const size_t cpuCount = PlatGetCpuCount(initState);

        InitPageInfoStore(initState);
        setupInfo.pmaSlots = reinterpret_cast<uintptr_t>(
            initState.VmAllocAnon(setupInfo.pmaEntries * sizeof(PageAccessCache::Slot)));
        setupInfo.perCpuStores = InitPerCpuStore(initState, cpuCount);
        setupInfo.perCpuStride = reinterpret_cast<uintptr_t>(KERNEL_CPULOCALS_END) 
            - reinterpret_cast<uintptr_t>(KERNEL_CPULOCALS_BEGIN);
        setupInfo.apStacks = InitApStacks(initState, cpuCount - 1);
        setupInfo.configCopy = CopyCommandLine(initState, loaderState.commandLine);
        MapKernelImage(initState, loaderState.kernelBase);

        ArchInitDomain0(initState);
        PlatInitDomain0(initState);
        InitPmFreeList(initState);

        setupInfo.virtBase = initState.vmAllocHead;
        return setupInfo;
    }

    struct AcpiTableAccess
    {
        char signature[4];
        Paddr paddr;
        void* vaddr;
    };

    static sl::Span<AcpiTableAccess> acpiTables;

    static void TryMapAcpiTables(uintptr_t& virtBase)
    {
        NPK_ASSERT(acpiTables.Empty());

        auto rsdpPhys = GetConfigRoot(ConfigRootType::Rsdp);
        if (!rsdpPhys.HasValue())
            return;

        char rsdpBuff[sizeof(Rsdp)];
        NPK_CHECK(CopyFromPhysical(*rsdpPhys, rsdpBuff) == sizeof(Rsdp), );
        Rsdp* rsdp = reinterpret_cast<Rsdp*>(rsdpBuff);

        Paddr ptrsBase;
        size_t ptrsCount;
        size_t ptrSize;
        if (rsdp->revision == 0 || rsdp->xsdt == 0)
        {
            char rsdtBuff[sizeof(Rsdt)];
            NPK_CHECK(sizeof(Rsdt) == CopyFromPhysical(rsdp->rsdt, rsdtBuff), );
            Rsdt* rsdt = reinterpret_cast<Rsdt*>(rsdtBuff);

            ptrsCount = (rsdt->length - sizeof(Sdt)) / sizeof(uint32_t);
            ptrSize = 4;
            ptrsBase = rsdp->rsdt + sizeof(Sdt);
        }
        else
        {
            char xsdtBuff[sizeof(Xsdt)];
            NPK_CHECK(sizeof(Xsdt) == CopyFromPhysical(rsdp->xsdt, xsdtBuff), );
            Xsdt* xsdt = reinterpret_cast<Xsdt*>(xsdtBuff);

            ptrsCount = (xsdt->length - sizeof(Sdt)) / sizeof(uint64_t);
            ptrSize = 8;
            ptrsBase = rsdp->xsdt + sizeof(Sdt);
        }
        Log("Acpi sdt config: %s has %zux %zu-byte addresses.", LogLevel::Verbose,
            ptrSize == 4 ? "rsdt" : "xsdt", ptrsCount, ptrSize);

        acpiTables = sl::Span<AcpiTableAccess>(reinterpret_cast<AcpiTableAccess*>(virtBase), ptrsCount);
        for (size_t i = 0; i < ptrsCount * sizeof(AcpiTableAccess); i += PageSize(), virtBase += PageSize())
        {
            const auto page = AllocPage(false);
            const auto error = ArchAddMap(MyKernelMap(), virtBase, LookupPagePaddr(page), MmuFlag::Write);
            NPK_ASSERT(error == MmuError::Success);
        }

        for (size_t i = 0; i < ptrsCount; i++)
        {
            const Paddr ptrPaddr = ptrsBase + ptrSize * i;

            Paddr sdtPaddr;
            sl::Span<char> sdtPtrBuff { reinterpret_cast<char*>(&sdtPaddr), ptrSize };
            NPK_CHECK(CopyFromPhysical(ptrPaddr, sdtPtrBuff)
                == sizeof(Paddr), );

            acpiTables[i].paddr = sdtPaddr;

            Sdt sdt;
            sl::Span<char> sdtBuff { reinterpret_cast<char*>(&sdt), sizeof(sdt) };
            NPK_CHECK(CopyFromPhysical(sdtPaddr, sdtBuff) == sizeof(Sdt), );
            sl::MemCopy(acpiTables[i].signature, sdt.signature, 4);

            acpiTables[i].vaddr = reinterpret_cast<void*>(virtBase);
            const Paddr sdtTop = sdtPaddr + sdt.length;
            for (size_t m = AlignDownPage(sdtPaddr); m < sdtTop; m += PageSize(), virtBase += PageSize())
                NPK_CHECK(ArchAddMap(MyKernelMap(), virtBase, m, {}) == MmuError::Success, );

            const auto conv = sl::ConvertUnits(sdt.length);
            Log("Mapped acpi table: %.4s v%u, %p -> 0x%tx, %zu.%zu %sB", LogLevel::Info, sdt.signature,
                sdt.revision, acpiTables[i].vaddr, acpiTables[i].paddr, conv.major, conv.minor, conv.prefix);
        }
    }

    sl::Opt<Sdt*> GetAcpiTable(sl::StringSpan signature)
    {
        for (size_t i = 0; i < acpiTables.Size(); i++)
        {
            if (sl::MemCompare(acpiTables[i].signature, signature.Begin(), 4) != 0)
                continue;

            return static_cast<Sdt*>(acpiTables[i].vaddr);
        }

        return {};
    }

    CPU_LOCAL(MemoryDomain*, localMemoryDomain);

    MemoryDomain& MyMemoryDomain()
    {
        return **localMemoryDomain;
    }
}

extern "C"
{
    void KernelEntry()
    {
        using namespace Npk;

        ArchInitEarly();
        PlatInitEarly();
        PrintWelcome();

        const size_t globalCtorCount = 
            ((uintptr_t)&INIT_ARRAY_END - (uintptr_t)&INIT_ARRAY_BEGIN) / sizeof(void*);
        for (size_t i = 0; i < globalCtorCount; i++)
            INIT_ARRAY_BEGIN[i]();
        Log("Ran %zu global constructors.", LogLevel::Verbose, globalCtorCount);

        const auto loaderState = Loader::GetEntryState();
        SetConfigStore(loaderState.commandLine);
        rootPtrs[static_cast<size_t>(ConfigRootType::Rsdp)] = loaderState.rsdp;
        rootPtrs[static_cast<size_t>(ConfigRootType::Fdt)] = loaderState.fdt;

        const auto setupInfo = SetupDomain0(loaderState);
        ArchSetKernelMap({});

        SetMyLocals(setupInfo.perCpuStores, 0);
        localMemoryDomain = &domain0;
        InitPageAccessCache(setupInfo.pmaEntries, setupInfo.pmaSlots);
        SetConfigStore(setupInfo.configCopy);

        Log("System early init done, BSP is entering init thread.", LogLevel::Trace);
        ThreadContext idleContext {};
        SetIdleThread(&idleContext);
        SetCurrentThread(&idleContext);

        uintptr_t virtBase = setupInfo.virtBase;
        TryMapAcpiTables(virtBase);

        ArchInitFull(virtBase);
        PlatInitFull(virtBase);
        PlatBootAps(setupInfo.apStacks, setupInfo.perCpuStores, setupInfo.perCpuStride);
        //TODO: boot APs
        //TODO: init vmm - virtBase serves as top of bump allocated region

        Log("BSP init thread done, becoming idle thread", LogLevel::Trace);
        IntrsOn();
        while (true)
            WaitForIntr();
    }
}
