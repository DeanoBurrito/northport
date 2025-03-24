#include <core/Config.h>
#include <core/Clock.h>
#include <core/Log.h>
#include <core/Defs.h>
#include <core/PmAccess.h>
#include <core/Smp.h>
#include <hardware/Arch.h>
#include <hardware/Platform.h>
#include <interfaces/intra/BakedConstants.h>
#include <interfaces/intra/LinkerSymbols.h>
#include <interfaces/loader/Generic.h>
#include <UnitConverter.h>
#include <Memory.h>
#include <Maths.h>

namespace Npk
{
    constexpr size_t ProcessChunkSize = 32;
    constexpr size_t DefaultPmaEntries = 512;
    constexpr size_t KaslrShiftMin = 0;
    constexpr size_t KaslrShiftMax = 32;

    extern "C" char* BspStackTop;

    static uintptr_t GetNextSlide()
    {
        size_t shift = 0;
        if (!PalGetRandom({ reinterpret_cast<uint8_t*>(&shift), 1 }))
            return PageSize();

        return sl::Max(shift & (KaslrShiftMax - 1), KaslrShiftMin) << PfnShift();
    }

    Paddr earlyPmAllocHead = 0;
    size_t earlyPmAllocIndex = 0;

    static Paddr EarlyPageAlloc()
    {
        using namespace Loader;

        MemmapEntry entry;
        while (true)
        {
            const size_t valid = GetMemmapUsable({ &entry, 1 }, earlyPmAllocIndex);
            ASSERT(valid != 0, "OOM during early init");

            if (earlyPmAllocHead < entry.base)
                earlyPmAllocHead = entry.base;

            if (earlyPmAllocHead + PageSize() >= entry.base + entry.length)
            {
                earlyPmAllocIndex++;
                continue;
            }

            const Paddr ret = earlyPmAllocHead;
            earlyPmAllocHead += PageSize();
            return ret;
        }
    }

    MemoryDomain domain0;

    uintptr_t SetupDomain0(const Loader::LoaderData& loaderData)
    {
        using namespace Core;

        EarlyMmuEnvironment mmuEnv;
        mmuEnv.directMapBase = loaderData.directMapBase;
        mmuEnv.directMapLength = loaderData.directMapLength;
        mmuEnv.EarlyPmAlloc = EarlyPageAlloc;
        mmuEnv.dom0 = &domain0;

        uintptr_t vmAllocHead = EarlyMmuBegin(mmuEnv);
        domain0.zeroPage = EarlyPageAlloc();
        sl::MemSet(reinterpret_cast<void*>(domain0.zeroPage), 0, PageSize());

        do //1. Map the PageInfo region (think `struct page` or pfndb)
        {
            Loader::MemmapEntry store[ProcessChunkSize];
            size_t accum = 0;
            uintptr_t dbBase = vmAllocHead;
            domain0.infoDb = reinterpret_cast<PageInfo*>(dbBase);

            while (true)
            {
                const size_t count = Loader::GetMemmapUsable(store, accum);
                accum += count;

                for (size_t i = 0; i < count; i++)
                {
                    const auto entry = store[i];

                    //NOTE: this assumes usable entries are sorted by base address
                    if (accum == count && i == 0)
                        domain0.physOffset = entry.base;

                    const size_t infoBase = (entry.base - domain0.physOffset) >> PfnShift();
                    const size_t infoLength = entry.length >> PfnShift();
                    const uintptr_t mapTop = dbBase + AlignUpPage((infoBase + infoLength) * sizeof(PageInfo));

                    uintptr_t mapBase = dbBase + AlignDownPage(infoBase * sizeof(PageInfo));
                    mapBase = sl::Max(mapBase, vmAllocHead);
                    while (mapBase <= mapTop)
                    {
                        EarlyMmuMap(mmuEnv, mapBase, EarlyPageAlloc(), MmuFlag::Write);
                        mapBase += PageSize();
                    }

                    vmAllocHead = mapTop;
                }

                if (count < ProcessChunkSize)
                    break;
            }

            Log("PageInfo db: 0x%tx-0x%tx", LogLevel::Info, dbBase, vmAllocHead);
        } 
        while (false);
        vmAllocHead += GetNextSlide();

        do //2. Prep temporary mapping space by populating intermediate tables and map access cache metadata
        {
            const size_t pmaEntries = GetConfigNumber("kernel.pma.cache_entries", DefaultPmaEntries);
            domain0.pmaBase = vmAllocHead;

            for (size_t i = 0; i < pmaEntries; i++)
                EarlyMmuMap(mmuEnv, domain0.pmaBase + (i << PfnShift()), domain0.zeroPage, {});
            vmAllocHead += pmaEntries << PfnShift();

            const size_t metaSize = pmaEntries * sizeof(Core::PmaCache::Slot);
            for (size_t i = 0; i < metaSize; i += PageSize())
                EarlyMmuMap(mmuEnv, vmAllocHead + (i << PfnShift()), EarlyPageAlloc(), MmuFlag::Write);

            InitPmaCache({ reinterpret_cast<PmaCache::Slot*>(vmAllocHead), pmaEntries}, 
                domain0.zeroPage);
            vmAllocHead += metaSize;

            Log("Temp mappings: access=0x%tx, meta=0x%tx, %zu entries", LogLevel::Info,
                domain0.pmaBase, vmAllocHead - metaSize, pmaEntries);
        }
        while (false);
        vmAllocHead += GetNextSlide();

        do //3. Call arch and platform layer hooks to allow smp-discovery
        {
            const uintptr_t archBegin = vmAllocHead;
            ArchMappingEntry(mmuEnv, vmAllocHead);
            const uintptr_t platBegin = vmAllocHead;
            PalMappingEntry(mmuEnv, vmAllocHead);

            Log("Target-specific: arch=0x%tx (0x%zx used), plat=0x%tx (0x%zu used)",
                LogLevel::Info, archBegin, platBegin - archBegin, platBegin, vmAllocHead - platBegin);

        }
        while (false);

        do //4. Map memory for per-cpu data storage
        {
            const size_t localsSize = smpInfo.localsStride * smpInfo.cpuCount;
            smpInfo.localsBase += vmAllocHead;
            vmAllocHead += AlignUpPage(localsSize);

            for (size_t i = 0; i < localsSize; i += PageSize())
                EarlyMmuMap(mmuEnv, localsSize + i, EarlyPageAlloc(), MmuFlag::Write);

            Log("Cpu-local storage: 0x%tx, %zu cpu%s, 0x%zx bytes each", LogLevel::Info, 
                smpInfo.localsBase, smpInfo.cpuCount, smpInfo.cpuCount > 1 ? "s" : "",
                smpInfo.localsStride);

        }
        while (false);
        vmAllocHead += GetNextSlide();

        do //5. Map memory for kernel idle stacks.
        {
            vmAllocHead += PageSize(); //guard page
            smpInfo.idleStacksBase = vmAllocHead;

            //NOTE: the BSP's (cpu0) stack is special because its allocated in the kernel image
            const uintptr_t bspStack = reinterpret_cast<uintptr_t>(&BspStackTop) - KernelStackSize()
                - reinterpret_cast<uintptr_t>(KERNEL_BLOB_BEGIN) + loaderData.kernelPhysBase;
            for (size_t i = 0; i < KernelStackSize(); i += PageSize())
                EarlyMmuMap(mmuEnv, vmAllocHead + i, bspStack + i, MmuFlag::Write);
            vmAllocHead += KernelStackSize() + PageSize(); //dont forget the guard page

            for (size_t i = 1; i < smpInfo.cpuCount; i++)
            {
                for (size_t j = 0; j < KernelStackSize(); j += PageSize())
                    EarlyMmuMap(mmuEnv, vmAllocHead + j, EarlyPageAlloc(), MmuFlag::Write);
                vmAllocHead += KernelStackSize() + PageSize(); //guard page
            }

            Log("Idle stacks: 0x%tx, 0x%zx bytes each", LogLevel::Info, smpInfo.idleStacksBase,
                KernelStackSize());
        }
        while (false);
        vmAllocHead += GetNextSlide();

        do //6. Map the kernel image proper
        {
            const uintptr_t virtBase = reinterpret_cast<uintptr_t>(KERNEL_BLOB_BEGIN);
            const Paddr physBase = loaderData.kernelPhysBase;

            for (char* i = AlignDownPage(KERNEL_TEXT_BEGIN); i < KERNEL_TEXT_END; i += PageSize())
                EarlyMmuMap(mmuEnv, (uintptr_t)i, (Paddr)i - virtBase + physBase, MmuFlag::Execute);
            for (char* i = AlignDownPage(KERNEL_RODATA_BEGIN); i < KERNEL_RODATA_END; i += PageSize())
                EarlyMmuMap(mmuEnv, (uintptr_t)i, (Paddr)i - virtBase + physBase, {});
            for (char* i = AlignDownPage(KERNEL_DATA_BEGIN); i < KERNEL_DATA_END; i += PageSize())
                EarlyMmuMap(mmuEnv, (uintptr_t)i, (Paddr)i - virtBase + physBase, MmuFlag::Write);

            Log("Kernel image: vbase=0x%tx, pbase=0x%tx", LogLevel::Info, virtBase, physBase);
        }
        while (false);


        //TODO: add remaining free memory to free list
        //TODO: fixup pfndb entries for currently used pages (so we can reclaim some later)

        EarlyMmuEnd(mmuEnv);
        return vmAllocHead;
    }

    extern "C" void KernelEntry()
    {
        using namespace Core;

        ArchEarlyEntry();
        PalEarlyEntry();
        //TODO: asan

        Log("Northport kernel started: v%zu.%zu.%zu for %s-%s (%s, %s%s)", LogLevel::Info,
            versionMajor, versionMinor, versionRev, targetArchStr, targetPlatformStr,
            toolchainUsed, gitCommitShortHash, gitCommitDirty ? "-dirty" : "");

        const size_t globalCtorCount = ((uintptr_t)&INIT_ARRAY_END - (uintptr_t)&INIT_ARRAY_BEGIN) / sizeof(void*);
        for (size_t i = 0; i < globalCtorCount; i++)
            INIT_ARRAY_BEGIN[i]();
        Log("Ran %zu global constructors.", LogLevel::Verbose, globalCtorCount);

        //TODO: two-stage config init: one now with direct map, then copy it during domain init
        InitConfigStore(Loader::GetCommandLine()); //soft-init, where we reference the initial data (so truncation)
        
        Loader::LoaderData data {};
        Loader::GetData(data);

        //TODO: collect number of cpus, populate smpInfo

        const uintptr_t highestVaddrUsed = SetupDomain0(data);
        MmuActivate(domain0.kernelSpace, true);

        Log("---- KERNEL INIT DONE ----", LogLevel::Debug);
        Halt();
    }
}
