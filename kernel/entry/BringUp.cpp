#include <EntryPrivate.hpp>
#include <CorePrivate.hpp>
#include <Core.hpp>
#include <Debugger.hpp>
#include <Vm.hpp>
#include <AcpiTypes.hpp>
#include <Maths.hpp>
#include <Memory.hpp>
#include <UnitConverter.hpp>

/* If you're looking for where the kernel starts life (after any arch-specific
 * entrypoint), search this file for `void KernelEntry()`
 */
namespace Npk
{
    void DispatchInterrupt(size_t vector) { (void)vector; };
    void DispatchPageFault(uintptr_t addr, bool write) {(void)addr; (void)write;}

    SystemDomain domain0 {};

    void EarlyPanic(sl::StringSpan why)
    {
        IntrsOff();

        Log("Early panic occured: %.*s", LogLevel::Error, 
            (int)why.Size(), why.Begin());

        while (true)
            WaitForIntr();
    }

    using Loader::LoadState;

    static void SetupKernelAddressSpace(InitState& init, LoadState& loader)
    {
        using namespace Loader;

        //0. Map the kernel image
        const auto imageVbase = (uintptr_t)KERNEL_BLOB_BEGIN;
        const auto imagePbase = loader.kernelBase;
        NPK_EARLY_ASSERT(imageVbase >= imagePbase);
        const auto imageOffset = imageVbase - imagePbase;

        Log("Mapping kernel image:", LogLevel::Verbose);
        Log("%7s|%20s|%18s|%6s", LogLevel::Verbose,
            "Name", "Virtual Base", "Physical Base", "Flags");

        for (char* i = AlignDownPage(KERNEL_TEXT_BEGIN); i < KERNEL_TEXT_END;
            i += PageSize())
        {
            const Paddr paddr = (Paddr)i - imageOffset;
            const uintptr_t vaddr = (uintptr_t)i;
            const MmuFlags flags = MmuFlag::Fetch;

            if (i == AlignDownPage(KERNEL_TEXT_BEGIN))
            {
                Log("%7s|%#20tx|%#18tx|  r-x", LogLevel::Verbose, 
                    "text", vaddr, paddr);
            }

            HwEarlyMap(init, paddr, vaddr, flags);
        }

        for (char* i = AlignDownPage(KERNEL_RODATA_BEGIN); i <KERNEL_RODATA_END;
            i += PageSize())
        {
            const Paddr paddr = (Paddr)i - imageOffset;
            const uintptr_t vaddr = (uintptr_t)i;
            const MmuFlags flags = {};

            if (i == AlignDownPage(KERNEL_RODATA_BEGIN))
            {
                Log("%7s|%#20tx|%#18tx|  r--", LogLevel::Verbose, 
                    "rodata", vaddr, paddr);
            }

            HwEarlyMap(init, paddr, vaddr, flags);
        }

        for (char* i = AlignDownPage(KERNEL_DATA_BEGIN); i < KERNEL_DATA_END;
            i += PageSize())
        {
            const Paddr paddr = (Paddr)i - imageOffset;
            const uintptr_t vaddr = (uintptr_t)i;
            const MmuFlags flags = MmuFlag::Write;

            if (i == AlignDownPage(KERNEL_DATA_BEGIN))
            {
                Log("%7s|%#20tx|%#18tx|  rw-", LogLevel::Verbose, 
                    "data", vaddr, paddr);
            }

            HwEarlyMap(init, paddr, vaddr, flags);
        }

        //1. Copy command line to the new address space
        const size_t cmdlineSize = loader.commandLine.Size();
        char* cmdlineDest = init.VmAlloc(cmdlineSize);

        for (size_t i = 0; i < loader.commandLine.Size(); i += PageSize())
        {
            const Paddr page = init.PmAlloc();
            const size_t len = sl::Min(cmdlineSize - i, PageSize());

            sl::MemCopy(reinterpret_cast<void*>(page + init.dmBase),
                loader.commandLine.Begin() + i, len);
            HwEarlyMap(init, page, reinterpret_cast<uintptr_t>(cmdlineDest) + i,
                {});
        }
        init.mappedCmdLine = { cmdlineDest, cmdlineSize };

        Log("Command line copied to: %p, %zu bytes", LogLevel::Info, 
            cmdlineDest, cmdlineSize);

        //2. Allocate memory for page info struct storage
        constexpr size_t MaxLoaderRanges = 32;
        MemoryRange ranges[MaxLoaderRanges];
        Paddr minUsablePaddr = static_cast<Paddr>(~0);
        Paddr maxUsablePaddr = 0;
        size_t rangesBase = 0;

        while (true)
        {
            const size_t count = GetUsableRanges(ranges, rangesBase);
            rangesBase += count;

            for (size_t i = 0; i < count; i++)
            {
                const auto top = ranges[i].base + ranges[i].length;

                sl::MaxInPlace(maxUsablePaddr, top);
                sl::MinInPlace(minUsablePaddr, ranges[i].base);
            }

            if (count < MaxLoaderRanges)
                break;
        }

        const size_t pfndbSize = ((maxUsablePaddr - minUsablePaddr) >> 
            PfnShift()) * sizeof(PageInfo);
        domain0.physOffset = minUsablePaddr;
        domain0.pfndb = reinterpret_cast<PageInfo*>(init.VmAlloc(pfndbSize));

        const uintptr_t dbOffset = reinterpret_cast<uintptr_t>(domain0.pfndb);
        rangesBase = 0;
        while (true)
        {
            const size_t count = GetUsableRanges(ranges, rangesBase);
            rangesBase += count;

            for (size_t i = 0; i < count; i++)
            {
                Paddr base = ranges[i].base - domain0.physOffset;
                Paddr top = base + ranges[i].length;

                base = AlignDownPage((base >> PfnShift()) * sizeof(PageInfo));
                top = AlignUpPage((top >> PfnShift()) * sizeof(PageInfo));

                Log("PageInfo region: 0x%tx-0x%tx -> 0x%tx-0x%tx",
                    LogLevel::Info, ranges[i].base, ranges[i].base + 
                    ranges[i].length, base, top);

                for (Paddr s = base; s < top; s += PageSize())
                {
                    Paddr p = init.PmAlloc();
                    uintptr_t v = dbOffset + s;
                    HwEarlyMap(init, p, v, MmuFlag::Write);
                }
            }

            if (count < MaxLoaderRanges)
                break;
        }

        //3. Setup PMA (physical memory access)/temp mappings
        size_t pmaSlotsSize = init.pmaCount * sizeof(PageAccessCache::Slot);
        auto pmaSlots = init.VmAllocAnon(pmaSlotsSize);
        init.pmaSlots = reinterpret_cast<uintptr_t>(pmaSlots);

        //4. Init list of free pages
        rangesBase = init.pmAllocIndex;
        size_t totalPages = 0;

        Log("Populating PM freelist from bootloader map:", LogLevel::Verbose);
        Log("%9s|%18s|%12s|%12s", LogLevel::Verbose, 
            "New Pages", "Base Address", "Total Pages", "Total Size");

        while (true)
        {
            const size_t count = GetUsableRanges(ranges, rangesBase);
            rangesBase += count;

            for (size_t i = 0; i < count; i++)
            {
                const Paddr top = ranges[i].base + ranges[i].length;
                const Paddr base = sl::Max(ranges[i].base, init.pmAllocHead);
                const size_t pageCount = (top - base) >> PfnShift();

                totalPages += pageCount;
                const auto conv = sl::ConvertUnits(totalPages << PfnShift());
                Log("%9zu|%#18tx|%12zu|%4zu.%03zu %sB", LogLevel::Verbose,
                    pageCount, base, totalPages, conv.major, conv.minor, 
                    conv.prefix);

                const auto loaderMap = HwKernelMap({});

                PageInfo* info = LookupPageInfo(base);
                info->pm.count = pageCount;
                domain0.freeLists.free.PushBack(info);
                domain0.freeLists.pageCount += pageCount;

                HwKernelMap(loaderMap);
            }

            if (count < MaxLoaderRanges)
                break;
        }

        const auto conv = sl::ConvertUnits(totalPages << PfnShift());
        const auto usedConv = sl::ConvertUnits(init.usedPages << PfnShift());
        Log("%zu.%zu %sB usable memory, %zu.%zu %sB used by address space init",
            LogLevel::Info, conv.major, conv.minor, conv.prefix,
            usedConv.major, usedConv.minor, usedConv.prefix);
    }

    static PerCpuData InitPerCpuData(uintptr_t& virtBase)
    {
        const size_t cpus = HwGetCpuCount();
        Log("Setting up control structures for %zu cpu%s.", LogLevel::Info,
            cpus, cpus != 1 ? "s" : "");

        //0. allocate and map stacks for AP idle threads
        //We dont allocate a stack for the BSP since we're already using it,
        //as its part of the kernel image.
        const size_t stackStride = KernelStackSize() + PageSize();
        virtBase += PageSize(); //guard page before the first stack
        const uintptr_t stacksBase = virtBase;

        for (size_t i = 0; i < cpus - 1; i++)
        {
            for (size_t p = 0; p < KernelStackPages(); p++)
            {
                auto page = AllocPage(false);
                auto paddr = LookupPagePaddr(page);
                SetKernelMap(virtBase, paddr, VmFlag::Write);

                virtBase += PageSize();
            }

            virtBase += PageSize();
        }

        Log("Idle stacks mapped: 0x%zx B each", LogLevel::Info,
            KernelStackSize());

        //1. allocate space for AP cpu-local storage
        //The BSP doesnt need local storage allocated for it, since it
        //uses the original storage thats part of the kernel image.
        const auto localsBegin = (uintptr_t)KERNEL_CPULOCALS_BEGIN;
        const auto localsEnd = (uintptr_t)KERNEL_CPULOCALS_END;
        const size_t localsStride = sl::AlignUp(localsEnd - localsBegin, 64);
        const size_t localsSize = localsStride * (cpus - 1);
        const uintptr_t localsBase = virtBase;

        for (size_t i = 0; i < localsSize; i += PageSize())
        {
            auto page = AllocPage(false);

            auto access = AccessPage(page);
            NPK_ASSERT(access.Valid());
            sl::MemSet(access->value, 0, PageSize());

            auto paddr = LookupPagePaddr(page);
            SetKernelMap(virtBase, paddr, VmFlag::Write);
            virtBase += PageSize();
        }

        const auto conv = sl::ConvertUnits(localsStride);
        Log("Per-cpu stores mapped: %zu.%zu %sB each", LogLevel::Info,
            conv.major, conv.minor, conv.prefix);

        //2. allocate space for smp control blocks
        const size_t controlsSize = sizeof(SmpControl) * cpus;
        const uintptr_t controlsBase = virtBase;

        for (size_t i = 0; i < controlsSize; i += PageSize())
        {
            auto page = AllocPage(false);

            auto access = AccessPage(page);
            NPK_ASSERT(access.Valid());
            sl::MemSet(access->value, 0, PageSize());

            auto paddr = LookupPagePaddr(page);
            SetKernelMap(virtBase, paddr, VmFlag::Write);
            virtBase += PageSize();
        }

        domain0.smpBase = 0;
        domain0.smpControls = { reinterpret_cast<SmpControl*>(controlsBase), 
            cpus };
        for (size_t i = 0; i < cpus; i++)
            new(&domain0.smpControls[i]) SmpControl();

        return 
        {
            .localsBase = localsBase,
            .apStacksBase = stacksBase,
            .localsStride = localsStride,
            .stackStride = stackStride,
        };
    }

    static void PrintWelcome()
    {
        constexpr const char* Banner[] = {
#if 0
R"(888b    888                  888    888                                888   )",
R"(8888b   888                  888    888                                888   )",
R"(88888b  888                  888    888                                888   )",
R"(888Y88b 888  .d88b.  888d888 888888 88888b.  88888b.   .d88b.  888d888 888888)",
R"(888 Y88b888 d88""88b 888P"   888    888 "88b 888 "88b d88""88b 888P"   888   )",
R"(888  Y88888 888  888 888     888    888  888 888  888 888  888 888     888   )",
R"(888   Y8888 Y88..88P 888     Y88b.  888  888 888 d88P Y88..88P 888     Y88b. )",
R"(888    Y888  "Y88P"  888      "Y888 888  888 88888P"   "Y88P"  888      "Y888)",
R"(                                             888                             )",
R"(                                             888                             )",
R"(                                             888                      )"
#endif
            };

        const size_t bannerLines = sizeof(Banner) / sizeof(char*);
        if (bannerLines > 0)
        {
            Log("Welcome to ...", LogLevel::Info);
            for (size_t i = 0; i < bannerLines; i++)
            {
                if (i != bannerLines - 1)
                    Log("%s", LogLevel::Info, Banner[i]);
                else
                {
                    Log("%s v%zu.%zu.%zu", LogLevel::Info, Banner[i],
                        versionMajor, versionMinor, versionRev);
                }
            }
        }
        else
        {
            Log("Northport kernel v%zu.%zu.%zu starting ...", LogLevel::Info,
                versionMajor, versionMinor, versionRev);
        }
        Log("Compiler flags: %s", LogLevel::Verbose, compileFlags);
        Log("Base Commit%s: %s", LogLevel::Verbose, gitDirty ? " (dirty)" : "",
            gitHash);
    }

    CPU_LOCAL(SystemDomain*, localSystemDomain);

    SystemDomain& MySystemDomain()
    {
        return **localSystemDomain;
    }

    void BringCpuOnline(ThreadContext* idle)
    {
        localSystemDomain = &domain0; //TODO: multi-domain

        Private::InitLocalScheduler(idle);
        SetCurrentThread(idle);
        Log("Cpu %zu is online and available.", LogLevel::Info, MyCoreId());
    }

    extern "C" void KernelEntry()
    {
        InitState initState {};
        auto loadState = Loader::GetEntryState();

        SetConfigStore(loadState.commandLine, true);
        HwInitEarly();
        PrintWelcome();

        if (loadState.timeOffset.HasValue())
            SetTimeOffset({ *loadState.timeOffset });

        size_t ctorCount = 0;
        for (auto it = INIT_ARRAY_BEGIN; it != INIT_ARRAY_END; it++)
        {
            it[0]();
            ctorCount++;
        }
        Log("Ran %zu global constructor%s.", LogLevel::Verbose, ctorCount,
            ctorCount == 1 ? "" : "s");

        SetConfigRoot(loadState); //TODO: have this map config-tables/data, call it later (after BringCpuOnline)

        initState.dmBase = loadState.directMapBase;
        initState.usedPages = 0;
        initState.pmaCount = ReadConfigUint("npk.pm.temp_mapping_count", 512);
        initState.vmAllocHead = HwInitBspMmu(initState, initState.pmaCount);
        domain0.zeroPage = initState.PmAlloc();

        SetupKernelAddressSpace(initState, loadState);
        HwKernelMap({});

        HwSetMyLocals((uintptr_t)KERNEL_CPULOCALS_BEGIN, loadState.bspId);
        localSystemDomain = &domain0;
        InitPageAccessCache(initState.pmaCount, initState.pmaSlots);
        SetConfigStore(initState.mappedCmdLine, false);

        uintptr_t virtBase = initState.vmAllocHead;

        TryMapAcpiTables(virtBase);
        if (loadState.efiTable.HasValue())
            TryEnableEfiRtServices(*loadState.efiTable, virtBase);

        const auto smpData = InitPerCpuData(virtBase);
        ArchInitFull(virtBase);
        PlatInitFull(virtBase);
        HwBootAps(virtBase, smpData);
        InitDebugger();

        ThreadContext idleContext {};
        BringCpuOnline(&idleContext);

        Log("BSP init done, entering idle thread.", LogLevel::Trace);
        IntrsOn();
        while (true)
            WaitForIntr();
    }
}
