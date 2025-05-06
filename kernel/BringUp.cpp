#include <KernelApi.hpp>
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
                Log("%9zu|%#18tx|%12zu|%4zu.%zu %sB", LogLevel::Verbose,
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

        InitPageInfoStore(initState);
        setupInfo.pmaSlots = reinterpret_cast<uintptr_t>(
            initState.VmAllocAnon(setupInfo.pmaEntries * sizeof(PageAccessCache::Slot)));
        setupInfo.perCpuStores = InitPerCpuStore(initState, 1);
        setupInfo.apStacks = InitApStacks(initState, 0);
        setupInfo.configCopy = CopyCommandLine(initState, loaderState.commandLine);
        MapKernelImage(initState, loaderState.kernelBase);

        ArchInitDomain0(initState);
        PlatInitDomain0(initState);
        InitPmFreeList(initState);

        setupInfo.virtBase = initState.vmAllocHead;
        return setupInfo;
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
        ArchInitFull(virtBase);
        PlatInitFull(virtBase);
        //TODO: boot APs
        //TODO: init vmm - virtBase serves as top of bump allocated region

        Log("BSP init thread done, becoming idle thread", LogLevel::Trace);
        IntrsOn();
        while (true)
            WaitForIntr();
    }
}
