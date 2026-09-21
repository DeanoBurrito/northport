#include <private/Entry.hpp>
#include <private/Core.hpp>
#include <Debugger.hpp>
#include <private/Namespace.hpp>
#include <private/Process.hpp>
#include <Video.hpp>
#include <Vm.hpp>
#include <lib/AcpiTypes.hpp>
#include <lib/Maths.hpp>
#include <lib/Memory.hpp>
#include <lib/Units.hpp>

/* If you're looking for where the kernel starts life (after any arch-specific
 * entrypoint), search this file for `void KernelEntry()`
 */
namespace Npk
{
    constexpr size_t MaxInitPhases = 8;

    struct InitPhase
    {
        const char* name;
        size_t pagesUsed;
        size_t vmBytes;
    };

    struct InitUsage
    {
        size_t phaseCount;
        size_t pagesUsed;
        size_t vmBase;
        size_t vmHead;
        InitPhase phases[MaxInitPhases];
    };

    static void EndInitPhase(InitUsage& usage, const char* name,
        const InitState& state)
    {
        NPK_EARLY_ASSERT(usage.phaseCount < MaxInitPhases);

        auto& phase = usage.phases[usage.phaseCount++];
        phase.name = name;
        phase.pagesUsed = state.usedPages - usage.pagesUsed;
        phase.vmBytes = state.vmAllocHead - usage.vmHead;

        usage.pagesUsed = state.usedPages;
        usage.vmHead = state.vmAllocHead;
    }

    static void LogInitUsage(const InitUsage& usage, size_t initPages,
        size_t usablePages)
    {
        struct Percentage 
        {
            size_t major;
            size_t minor;
        };

        auto MakePercent = [](size_t part, size_t whole) -> Percentage
        {
            if (whole == 0)
                return {};

            size_t accum = (part / whole) * 10000 + ((part % whole) * 10000) 
                / whole;

            return { accum / 100, accum % 100 };
        };

        Log("Address space usage: 0x%tx-0x%tx", LogLevel::Verbose,
            usage.vmBase, usage.vmHead);
        Log("%16s|%9s|%14s|%8s|%14s", LogLevel::Verbose, "Phase", "Pages",
            "Memory", "Memory %", "Address Space");

        for (size_t i = 0; i < usage.phaseCount; i++)
        {
            const auto& phase = usage.phases[i];

            auto conv = sl::ConvertUnits(phase.pagesUsed << PfnShift());
            auto percent = MakePercent(phase.pagesUsed, usablePages);
            auto vmConv = sl::ConvertUnits(phase.vmBytes);

            Log("%16s|%9zu|%6zu.%03zu %2sB|%4zu.%02zu%%|%6zu.%03zu %2sB",
                LogLevel::Verbose, phase.name, phase.pagesUsed, conv.major,
                conv.minor, conv.prefix, percent.major, percent.minor,
                vmConv.major, vmConv.minor, vmConv.prefix);
        }

        auto conv = sl::ConvertUnits(usage.pagesUsed << PfnShift());
        auto percent = MakePercent(initPages, usablePages);
        auto vmConv = sl::ConvertUnits(usage.vmHead - usage.vmBase);
        Log("%16s|%9zu|%6zu.%03zu %2sB|%4zu.%02zu%%|%6zu.%03zu %2sB",
            LogLevel::Verbose, "total", usage.pagesUsed, conv.major, conv.minor,
            conv.prefix, percent.major, percent.minor, vmConv.major,
            vmConv.minor, vmConv.prefix);
    }

    static void LogImageSection(const char* name, char* begin, char* end,
        uintptr_t offset, const char* flags)
    {
        const auto base = reinterpret_cast<uintptr_t>(AlignDownPage(begin));
        const auto top = reinterpret_cast<uintptr_t>(AlignUpPage(end));
        const auto conv = sl::ConvertUnits(top - base);

        Log("%10s|%#20tx|%#18tx|%4zu.%03zu %2sB| %s", LogLevel::Verbose, name,
            base, base - offset, conv.major, conv.minor, conv.prefix, flags);
    }

    void DispatchInterrupt(size_t vector) { (void)vector; };

    SystemDomain sysDomain0 {};

    void EarlyPanic(sl::StringSpan why)
    {
        IntrsOff();

        Log("Early panic occurred: %.*s", LogLevel::Error, 
            (int)why.Size(), why.Begin());

        while (true)
            WaitForIntr();
    }

    using Loader::LoadState;

    static void SetupKernelAddressSpace(InitState& init, LoadState& loader)
    {
        using namespace Loader;

        constexpr size_t MaxLoaderRanges = 32;
        MemoryRange ranges[MaxLoaderRanges];
        Paddr minUsablePaddr = static_cast<Paddr>(~0);
        Paddr maxUsablePaddr = 0;
        size_t usablePages = 0;
        size_t largestRangePages = 0;
        size_t usableRangeCount = 0;
        size_t rangesBase = 0;

        while (true)
        {
            const size_t count = GetUsableRanges(ranges, rangesBase);
            rangesBase += count;
            usableRangeCount += count;

            for (size_t i = 0; i < count; i++)
            {
                const auto top = ranges[i].base + ranges[i].length;
                sl::MaxInPlace(maxUsablePaddr, top);
                sl::MinInPlace(minUsablePaddr, ranges[i].base);

                const size_t pages = ranges[i].length >> PfnShift();
                sl::MaxInPlace(largestRangePages, pages);
                usablePages += pages;

                if (((ranges[i].base | ranges[i].length) & PageMask()) != 0)
                {
                    //this should never happen but just in case I write a buggy
                    //loaded in the future, this'll complain loudly.
                    Log("Usable memory range is not page aligned: 0x%tx, 0x%zx",
                        LogLevel::Warning, ranges[i].base, ranges[i].length);
                }
            }

            if (count < MaxLoaderRanges)
                break;
        }
        NPK_EARLY_ASSERT(usablePages > 0);

        auto conv = sl::ConvertUnits(usablePages << PfnShift());
        Log("Usable memory: %zu.%03zu %sB (%zu pages) in %zu range%s",
            LogLevel::Info, conv.major, conv.minor,
            conv.prefix, usablePages, usableRangeCount,
            usableRangeCount == 1 ? "" : "s");
        conv = sl::ConvertUnits(largestRangePages << PfnShift());
        Log("Usable span: 0x%tx-0x%tx, largest range is %zu.%03zu %sB",
            LogLevel::Info, minUsablePaddr, maxUsablePaddr, conv.major,
            conv.minor, conv.prefix);

        InitUsage usage {};
        usage.vmBase = init.vmAllocHead;
        usage.vmHead = usage.vmBase;
        EndInitPhase(usage, "MMU bootstrap", init);

        //0. Map the kernel image
        const auto imageVbase = (uintptr_t)KERNEL_BLOB_BEGIN;
        const auto imagePbase = loader.kernelBase;
        NPK_EARLY_ASSERT(imageVbase >= imagePbase);
        const auto imageOffset = imageVbase - imagePbase;

        conv = sl::ConvertUnits(KERNEL_BLOB_END - KERNEL_BLOB_BEGIN);
        Log("Mapping kernel image: %zu.%03zu %sB, slide=0x%tx", LogLevel::Info,
            conv.major, conv.minor, conv.prefix, imageOffset);
        Log("%10s|%20s|%18s|%12s|%6s", LogLevel::Verbose,
            "Name", "Virt Base", "Phys Base", "Size", "Flags");
        LogImageSection("text", KERNEL_TEXT_BEGIN, KERNEL_TEXT_END, 
            imageOffset, "r-x");
        LogImageSection("rodata", KERNEL_RODATA_BEGIN, KERNEL_RODATA_END, 
            imageOffset, "r--");
        LogImageSection("data", KERNEL_DATA_BEGIN, KERNEL_DATA_END, 
            imageOffset, "rw-");

        for (char* i = AlignDownPage(KERNEL_TEXT_BEGIN); i < KERNEL_TEXT_END;
            i += PageSize())
        {
            const Paddr paddr = (Paddr)i - imageOffset;
            const uintptr_t vaddr = (uintptr_t)i;
            const MmuPermissions perms = MmuPermission::Write 
                | MmuPermission::Fetch;

            HwEarlyMap(init, paddr, vaddr, perms, {});
        }

        for (char* i = AlignDownPage(KERNEL_RODATA_BEGIN); i <KERNEL_RODATA_END;
            i += PageSize())
        {
            const Paddr paddr = (Paddr)i - imageOffset;
            const uintptr_t vaddr = (uintptr_t)i;
            const MmuPermissions perms = {};

            HwEarlyMap(init, paddr, vaddr, perms, {});
        }

        for (char* i = AlignDownPage(KERNEL_DATA_BEGIN); i < KERNEL_DATA_END;
            i += PageSize())
        {
            const Paddr paddr = (Paddr)i - imageOffset;
            const uintptr_t vaddr = (uintptr_t)i;
            const MmuPermissions perms = MmuPermission::Write;

            HwEarlyMap(init, paddr, vaddr, perms, {});
        }

        EndInitPhase(usage, "Kernel image", init);

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
                {}, {});
        }
        init.mappedCmdLine = { cmdlineDest, cmdlineSize };

        Log("Command line copied to: %p, %zu bytes", LogLevel::Info, 
            cmdlineDest, cmdlineSize);
        EndInitPhase(usage, "Command line", init);

        //2. Allocate memory for page info struct storage
        const size_t pfndbSize = AlignUpPage(((maxUsablePaddr - minUsablePaddr)
            >> PfnShift()) * sizeof(PageInfo));
        sysDomain0.physOffset = minUsablePaddr;
        sysDomain0.pfndbCount = maxUsablePaddr - minUsablePaddr;
        sysDomain0.pfndb = reinterpret_cast<PageInfo*>(init.VmAlloc(pfndbSize));

        //to be able to implement `PaddrHasPageInfo()` ("is this physical addr
        //ram managed by the kernel, or is it something else?") we need to be
        //able to probe addresses within the pfndb that might not be real page
        //info structs, so we store a poison value for partial pages and map
        //a single page of poison values to page aligned gaps.
        const Paddr poison = init.PmAlloc();
        const auto poisonValue = reinterpret_cast<void*>(1);
        auto* poisonEntries = reinterpret_cast<PageInfo*>(init.dmBase + poison);
        for (size_t i = 0; i < PageSize() / sizeof(PageInfo); i++)
            poisonEntries[i].mmList.next = poisonValue;

        const uintptr_t dbOffset = 
            reinterpret_cast<uintptr_t>(sysDomain0.pfndb);
        Paddr prevRangeTop = 0;
        Paddr prevDbTop = 0;
        rangesBase = 0;
        while (true)
        {
            const size_t count = GetUsableRanges(ranges, rangesBase);
            rangesBase += count;

            for (size_t i = 0; i < count; i++)
            {
                NPK_ASSERT(ranges[i].base >= prevRangeTop);
                prevRangeTop = ranges[i].base + ranges[i].length;

                Paddr base = ranges[i].base - sysDomain0.physOffset;
                Paddr top = base + ranges[i].length;

                base = AlignDownPage((base >> PfnShift()) * sizeof(PageInfo));
                top = AlignUpPage((top >> PfnShift()) * sizeof(PageInfo));

                if (base > prevDbTop)
                {
                    Log("Poisoned region: 0x%tx-0x%tx",
                        LogLevel::Info, prevDbTop, base);
                    HwEarlyMapPoison(init, poison, dbOffset + prevDbTop,
                        base - prevDbTop);
                }
                prevDbTop = top;

                Log("PageInfo region: 0x%tx-0x%tx (phys 0x%tx-0x%tx)",
                    LogLevel::Info, base, top, ranges[i].base, ranges[i].base
                    + ranges[i].length);

                for (Paddr s = base; s < top; s += PageSize())
                {
                    Paddr p = init.PmAlloc();
                    uintptr_t v = dbOffset + s;
                    HwEarlyMap(init, p, v, MmuPermission::Write, {});
                }
            }

            if (count < MaxLoaderRanges)
                break;
        }

        if (prevDbTop < pfndbSize)
        {
            Log("Poisoned region: 0x%tx-0x%tx",
                LogLevel::Info, prevDbTop, pfndbSize);
            HwEarlyMapPoison(init, poison, dbOffset + prevDbTop,
                pfndbSize - prevDbTop);
        }
        EndInitPhase(usage, "PageInfo init", init);

        //3. Setup PMA (physical memory access)/temp mappings for the bsp.
        size_t pmaSlotsSize = init.pmaCount * sizeof(PageAccessCache::Slot);
        auto pmaSlots = init.VmAllocAnon(pmaSlotsSize);
        init.pmaSlots = reinterpret_cast<uintptr_t>(pmaSlots);

        const size_t granule = HwTempMapGranularity() << PfnShift();
        init.vmAllocHead = sl::AlignUp(init.vmAllocHead, granule);

        auto pmaWindow = init.VmAlloc(init.pmaCount << PfnShift());
        init.pmaBase = reinterpret_cast<uintptr_t>(pmaWindow);

        auto result = HwMakeTempMapSpace(&init.bspTempMapToken, &init,
            init.vmAllocHead, init.pmaBase, init.pmaCount);
        NPK_ASSERT(result == NpkStatus::Success);

        EndInitPhase(usage, "BSP temp maps", init);

        //4. Init list of free pages
        const size_t startIndex = init.pmAllocIndex;
        MemoryRange* gathered = ranges;
        size_t rangeCount = GetUsableRanges(ranges, startIndex);

        //its uncommon but on some systems we can end up with a load of usable
        //memory ranges. In this case the stack allocated array isn't big enough
        //so we carve one into the kernel's runtime page map and copy the map
        //data there.
        if (rangeCount == MaxLoaderRanges)
        {
            const size_t rangesPerPage = PageSize() / sizeof(MemoryRange);
            NPK_ASSERT(PageSize() % sizeof(MemoryRange) == 0);

            rangesBase = rangeCount;
            while (true)
            {
                const size_t count = GetUsableRanges(ranges, rangesBase);
                rangesBase += count;
                rangeCount += count;

                if (count != MaxLoaderRanges)
                    break;
            }

            gathered = reinterpret_cast<MemoryRange*>(
                init.VmAlloc(rangeCount * sizeof(MemoryRange)));

            MemoryRange* spillPage = nullptr;
            size_t spilled = 0;
            for (size_t base = startIndex;;)
            {
                const size_t count = GetUsableRanges(ranges, base);
                base += count;

                for (size_t i = 0; i < count; i++, spilled++)
                {
                    if (spilled % rangesPerPage != 0)
                    {
                        spillPage[spilled & rangesPerPage] = ranges[i];

                        continue;
                    }

                    const Paddr page = init.PmAlloc();
                    const uintptr_t vaddr =
                        reinterpret_cast<uintptr_t>(gathered)
                        + spilled * sizeof(MemoryRange);

                    HwEarlyMap(init, page, vaddr, MmuPermission::Write, {});

                    spillPage = reinterpret_cast<MemoryRange*>(
                        init.dmBase + page);
                }

                if (count < MaxLoaderRanges)
                    break;
            }

            EndInitPhase(usage, "Memmap spill", init);
        }

        //switch to the runtime kernel map, after this point we're no longer
        //able to access loader data + InitState allocators.
        HwCompleteBspMmuInit();

        Log("Populating PM freelist from bootloader map:", LogLevel::Verbose);
        Log("%9s|%18s|%12s|%12s", LogLevel::Verbose, 
            "New Pages", "Base Address", "Total Pages", "Total Size");

        size_t totalPages = 0;
        for (size_t i = 0; i < rangeCount; i++)
        {
            const Paddr top = gathered[i].base + gathered[i].length;
            const Paddr base = sl::Max(gathered[i].base, init.pmAllocHead);
            const size_t pageCount = (top - base) >> PfnShift();

            if (pageCount == 0)
                continue;

            totalPages += pageCount;
            const auto conv = sl::ConvertUnits(totalPages << PfnShift());
            Log("%9zu|%#18tx|%12zu|%4zu.%03zu %sB", LogLevel::Verbose,
                pageCount, base, totalPages, conv.major, conv.minor,
                conv.prefix);

            PageInfo* info = LookupPageInfo(base);
            info->pm.count = pageCount;
            sysDomain0.freeLists.free.PushBack(info);
            sysDomain0.freeLists.pageCount += pageCount;
        }

        const size_t initPages = init.usedPages;
        LogInitUsage(usage, initPages, usablePages);
        
        if (totalPages + initPages < usablePages)
        {
            const size_t lostPages = usablePages - (totalPages + initPages);
            conv = sl::ConvertUnits(lostPages << PfnShift());

            Log("%zu page%s (%zu.%03zu %sB) of usable memory unaccounted for.",
                LogLevel::Warning, lostPages, lostPages == 1 ? "" : "s",
                conv.major, conv.minor, conv.prefix);
        }
    }

    static PerCpuConfig InitPerCpuData(uintptr_t& virtBase, size_t pmaSlots)
    {
        const size_t cpus = HwGetCpuCount();
        Log("Setting up control structures for %zu cpu%s.", LogLevel::Info,
            cpus, cpus != 1 ? "s" : "");

        //0. Allocate and map stacks for AP idle threads
        //We dont allocate a stack for the BSP since we're already using it,
        //as its part of the kernel image.
        const size_t stackStride = KernelStackSize() + PageSize();
        virtBase += PageSize(); //guard page before the first stack
        const uintptr_t stacksBase = virtBase;

        const auto prevIpl = RaiseIpl(Ipl::Dpc);
        for (size_t i = 0; i < cpus - 1; i++)
        {
            for (size_t p = 0; p < KernelStackPages(); p++)
            {
                auto page = AllocPage(true);
                NPK_ASSERT(page != nullptr);
                auto paddr = LookupPagePaddr(page);

                auto result = SetKernelMap(virtBase + (p << PfnShift()), paddr,
                    VmFlag::Write);
                NPK_ASSERT(result == NpkStatus::Success);
            }

            virtBase += stackStride;
        }

        Log("Idle stacks mapped: 0x%zx B each", LogLevel::Info,
            KernelStackSize());

        //1. Allocate space for AP cpu-local storage
        //The BSP doesn;t need local storage allocated for it, since it
        //uses the original storage thats part of the kernel image.
        const auto localsBegin = (uintptr_t)KERNEL_CPULOCALS_BEGIN;
        const auto localsEnd = (uintptr_t)KERNEL_CPULOCALS_END;
        const size_t localsStride = sl::AlignUp(localsEnd - localsBegin,
            HwGetStaticCacheLineSize());
        const size_t localsSize = localsStride * (cpus - 1);
        const uintptr_t localsBase = virtBase;

        for (size_t i = 0; i < localsSize; i += PageSize())
        {
            auto page = AllocPage(true);
            NPK_ASSERT(page != nullptr);

            auto paddr = LookupPagePaddr(page);
            auto result = SetKernelMap(virtBase, paddr, VmFlag::Write);
            NPK_ASSERT(result == NpkStatus::Success);

            virtBase += PageSize();
        }

        const auto conv = sl::ConvertUnits(localsStride);
        Log("Per-cpu stores mapped: %zu.%zu %sB each", LogLevel::Info,
            conv.major, conv.minor, conv.prefix);

        //2. Allocate inter-cpu control blocks
        const size_t controlsSize = sizeof(SmpControl) * cpus;
        const uintptr_t controlsBase = virtBase;

        for (size_t i = 0; i < controlsSize; i += PageSize())
        {
            auto page = AllocPage(true);
            NPK_ASSERT(page != nullptr);

            auto paddr = LookupPagePaddr(page);
            auto result = SetKernelMap(virtBase, paddr, VmFlag::Write);
            NPK_ASSERT(result == NpkStatus::Success);

            virtBase += PageSize();
        }

        sysDomain0.smpBase = 0;
        sysDomain0.smpControls = { reinterpret_cast<SmpControl*>(controlsBase), 
            cpus };
        for (size_t i = 0; i < cpus; i++)
            new(&sysDomain0.smpControls[i]) SmpControl();

        //3. Allocate temp mapping window and cache slots.
        const size_t tempSlotsSize = pmaSlots * sizeof(PageAccessCache::Slot);
        const size_t tempSlotsStride = sl::AlignUp(tempSlotsSize,
            HwGetStaticCacheLineSize());
        const uintptr_t tempSlotsBase = virtBase;

        for (size_t i = 0; i < tempSlotsStride * (cpus - 1); i += PageSize())
        {
            auto page = AllocPage(true);
            NPK_ASSERT(page != nullptr);

            auto paddr = LookupPagePaddr(page);
            auto result = SetKernelMap(virtBase, paddr, VmFlag::Write);
            NPK_ASSERT(result == NpkStatus::Success);

            virtBase += PageSize();
        }

        const uintptr_t tokensBase = virtBase;
        for (size_t i = 0; i < sizeof(void*) * (cpus - 1); i += PageSize())
        {
            auto page = AllocPage(true);
            NPK_ASSERT(page != nullptr);

            auto paddr = LookupPagePaddr(page);
            auto result = SetKernelMap(virtBase, paddr, VmFlag::Write);
            NPK_ASSERT(result == NpkStatus::Success);

            virtBase += PageSize();
        }
        sl::Span<void*> tokens(reinterpret_cast<void**>(tokensBase), cpus - 1);

        const size_t granule = HwTempMapGranularity() << PfnShift();
        NPK_ASSERT(pmaSlots % HwTempMapGranularity() == 0);

        virtBase = sl::AlignUp(virtBase, granule);
        const uintptr_t tempMapBase = virtBase;
        const size_t tempMapStride = pmaSlots << PfnShift();
        virtBase += tempMapStride * (cpus - 1);

        for (size_t i = 0; i < cpus - 1; i++)
        {
            auto result = HwMakeTempMapSpace(&tokens[i], nullptr, virtBase,
                tempMapBase + tempMapStride * i, pmaSlots);
            NPK_ASSERT(result == NpkStatus::Success);
        }

        Log("Temp map windows: 0x%zx B each (x%zu slots), stride=0x%zx B",
            LogLevel::Info, tempMapStride, pmaSlots, tempSlotsStride);

        LowerIpl(prevIpl);

        PerCpuConfig conf {};
        conf.cpuCount = cpus;
        conf.localsBase = localsBase;
        conf.localsStride = localsStride;
        conf.apStacksBase = stacksBase;
        conf.stackStride = stackStride;
        conf.tempMapBase = tempMapBase;
        conf.tempMapStride = tempMapStride;
        conf.tempSlotsBase = tempSlotsBase;
        conf.tempSlotsStride = tempSlotsStride;
        conf.tempSlotsCount = pmaSlots;
        conf.tempMapTokens = tokens;

        return conf;
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
    
    static void LogLoaderState(const LoadState& state)
    {
        Log("Loader state: direct map 0x%tx, kernel pbase 0x%tx, bsp id %zu",
            LogLevel::Verbose, state.directMapBase, state.kernelBase,
            state.bspId);

        Log("Loader config: rsdp=0x%tx, fdt=0x%tx, efi=0x%tx, module=0x%tx",
            LogLevel::Verbose,
            state.rsdp.HasValue() ? *state.rsdp : 0,
            state.fdt.HasValue() ? *state.fdt : 0,
            state.efi.HasValue() ? (*state.efi).systemTable : 0,
            state.moduleBlob.HasValue() ? *state.moduleBlob : 0);

        if (state.timeOffset.HasValue())
        {
            const auto offset = (*state.timeOffset).epoch;
            Log("Loader time offset: %zu ns", LogLevel::Verbose, offset);
        }
        else
            Log("Loader did not provide time offset.", LogLevel::Verbose);

        Log("Loader cmdline: %.*s", LogLevel::Verbose,
            (int)state.commandLine.Size(), state.commandLine.Begin());
    }

    CPU_LOCAL(SystemDomain*, localSystemDomain);

    SystemDomain& MySystemDomain()
    {
        return **localSystemDomain;
    }

    void InitLocalState(void* hwToken, uintptr_t slotsBase, size_t slotsCount,
        uintptr_t tempMapBase)
    {
        localSystemDomain = &sysDomain0; //TODO: multi-domain

        size_t ctorCount = 0;
        for (auto it = PREINIT_ARRAY_BEGIN; it != PREINIT_ARRAY_END; ++it)
        {
            it[0]();
            ctorCount++;
        }
        Log("Ran %zu local constructor%s.", LogLevel::Verbose, ctorCount,
            ctorCount == 1 ? "" : "s");

        HwSetTempMap(hwToken, tempMapBase, slotsCount);
        Private::ResetCycleAccounts(CycleAccount::Kernel);
        Private::InitPageAccessCache(slotsBase, slotsCount);

        Log("Cpu %zu has initialized local kernel state.", LogLevel::Info,
            MyCoreId());
    }

    void EnterIdleLoop()
    {
        auto& dom = MySystemDomain();

        Log("Cpu is entering idle loop.", LogLevel::Trace);
        while (true)
        {
            Private::DrainCleanupJobs();

            EnterNoEpochState(dom.rcu, MyRelativeCoreId());
            WaitForIntr();
            ExitNoEpochState(dom.rcu, MyRelativeCoreId());
        }

        NPK_UNREACHABLE();
    }

    void PerformFireworksTest(SimpleFramebuffer* fb);

    extern "C" void KernelEntry()
    {
        //1. Very early setup. There are some dependencies between the following
        //portions of code, most of them cannot be moved.
        InitState initState {};
        auto loadState = Loader::GetEntryState();

        SetConfigStore(loadState.commandLine, true);
        HwInitEarly();
        PrintWelcome();
        LogLoaderState(loadState);

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

        //2. Setup early allocators
        initState.dmBase = loadState.directMapBase;
        initState.usedPages = 0;

        const size_t pmaGranule = HwTempMapGranularity();
        initState.pmaCount = sl::AlignUp(ReadConfigUint("npk.pm.temp_map_slots",
            pmaGranule), pmaGranule);
        initState.vmAllocHead = HwInitBspMmu(initState);
        sysDomain0.zeroPage = initState.PmAlloc();

        //3. Setup kernel virtual address space: this function switches to it
        //internally, since the pmm freelist needs the kernel tables active.
        SetupKernelAddressSpace(initState, loadState);
        SetConfigStore(initState.mappedCmdLine, false);
        
        //4. Setup BSP local state. The storage for these is the original copy
        //in the kernel image, other cpus get an area of the same size but
        //zero filled in `InitPerCpuData()`.
        HwSetMyLocals((uintptr_t)KERNEL_CPULOCALS_BEGIN, loadState.bspId);
        InitLocalState(initState.bspTempMapToken, initState.pmaSlots,
            initState.pmaCount, initState.pmaBase);

        //5. Initialize discovery mechanisms from firmware (acpi, fdt, efi rt).
        uintptr_t virtBase = initState.vmAllocHead;

        SetConfigRoot(loadState);
        auto result = TryMapAcpiTables(virtBase);
        if (result != NpkStatus::Success)
            NPK_UNEXPECTED_STATUS(result, LogLevel::Error);
     
        if (loadState.efi.HasValue())
        {
            result = TryEnableEfiRuntimeServices(*loadState.efi, virtBase);
            if (result != NpkStatus::Success)
                NPK_UNEXPECTED_STATUS(result, LogLevel::Error);
        }
        else
            Log("EFI runtime services not available.", LogLevel::Info);

        //6. Discover and boot APs.
        const auto smpData = InitPerCpuData(virtBase, initState.pmaCount);
        HwInitFull(virtBase);
        const size_t bootedAps = StartAps(smpData, virtBase);
        if (bootedAps < MySystemDomain().smpControls.Size() - 1)
        {
            auto& controls = MySystemDomain().smpControls;

            Log("%zu of %zu APs booted, truncating live cpu count.",
                LogLevel::Warning, bootedAps, controls.Size() - 1);

            MySystemDomain().smpControls = controls.Subspan(0, bootedAps + 1);
            //NOTE: we do leak some memory here, in an ideal world we wouldn't.
        }

        //7. Start bringing higher level subsystems online.
        InitDebugger(virtBase);

        ThreadContext idleContext {};
        Private::InitLocalScheduler(&idleContext);
        SetCurrentThread(&idleContext);

        const uintptr_t lowBase = virtBase;
        const uintptr_t lowTop = AlignDownPage((uintptr_t)KERNEL_BLOB_BEGIN);
        const uintptr_t highBase = AlignUpPage((uintptr_t)KERNEL_BLOB_END);
        const uintptr_t highTop = AlignDownPage((uintptr_t)~0);
        InitKernelVmSpace(lowBase, lowTop - lowBase, highBase, 
            highTop - highBase);

        ResetEbrDomain(sysDomain0.rcu, sysDomain0.smpControls.Size(),
            [](EbrDomain& dom, size_t who) -> void 
            { 
                (void)dom; 

                who += MySystemDomain().smpBase;
                HwSendIpi(who);
            });

        HwLateInit();
        Private::InitNamespace();
        InitProcessSubsystem();

        //6. BSP initialization is complete.
        Log("BSP init done, loading init program.", LogLevel::Trace);
        IntrsOn();
        HwReleaseAps();

        //7. Load userspace init program.
        auto result = LoadInitProgram();
        if (result != NpkStatus::Success)
        {
            Panic("Failed to load init program, status=%u %s", nullptr,
                result, StatusStr(result));
        }
        */

        EnterIdleLoop();
    }
}
