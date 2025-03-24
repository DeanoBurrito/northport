#include <core/Acpi.h>
#include <core/Config.h>
#include <core/Clock.h>
#include <core/Log.h>
#include <core/Scheduler.h>
#include <core/Smp.h>
#include <core/PmAlloc.h>
#include <core/PmAccess.h>
#include <cpp/Asan.h>
#include <hardware/Arch.h>
#include <hardware/Platform.h>
#include <interfaces/intra/BakedConstants.h>
#include <interfaces/intra/LinkerSymbols.h>
#include <interfaces/loader/Generic.h>
#include <Maths.h>
#include <KernelThread.h>
#include <UnitConverter.h>

namespace Npk
{
    constexpr size_t IngestChunkSize = 32;
    constexpr size_t DefaultPmaEntries = 512;
    constexpr size_t DefaultMailCount = 512;
    
    extern sl::Span<MemoryDomain> memoryDomains;

    static void PerCoreEntry(size_t myId)
    {
        LocalMmuInit();
        MmuActivate(LocalDomain().kernelSpace, true);

        ArchInitCore(myId);
        PalInitCore(myId);

        /*
        Core::InitLocalClockQueue();

        Core::Scheduler* localSched = NewWired<Core::Scheduler>();
        auto maybeIdle = CreateKernelThread(IdleThreadEntry, nullptr);
        ASSERT_(localSched != nullptr && maybeIdle.HasValue());

        localSched->Init(*maybeIdle);
        SetLocalPtr(SubsysPtr::Scheduler, localSched);
        //TODO: init local intr routing, logging, heap caches, launch init thread + reclaim thread
        */
    }

    static uintptr_t GetNextKaslrSlide()
    {
        return 0; //TODO:
    }

    Paddr earlyPmAllocHead = 0;
    size_t earlyPmAllocIndex = 0;

    static Paddr EarlyPageAlloc()
    {
        Loader::MemmapEntry entry;

        while (true)
        {
            const size_t valid = Loader::GetUsableMemmap({ &entry, 1 }, earlyPmAllocIndex);
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

    static void IngestMemory(MemoryDomain& dom, sl::Span<Loader::MemmapEntry> entries)
    {
        for (size_t i = 0; i < entries.Size(); i++)
        {
            if (entries[i].length == 0)
                continue;

            const size_t pages = entries[i].length >> PfnShift();
            const size_t dbOffset = (entries[i].base - dom.physOffset) >> PfnShift();

            PageInfo* head = new(&dom.infoDb[dbOffset]) PageInfo{};
            head->pm.count = pages;

            const auto conv = sl::ConvertUnits(entries[i].length);
            Log("Ingesting physical memory: 0x%tx-0x%tx (0x%zx / %zu.%zu %sB)",
                LogLevel::Info, entries[i].base, entries[i].base + entries[i].length,
                entries[i].length, conv.major, conv.minor, conv.prefix);

            sl::ScopedLock scopeLock(dom.freeLists.lock);
            dom.freeLists.pageCount += pages;
            dom.freeLists.free.PushBack(head);
        }
    }

    static uintptr_t SetupMemoryDomain0()
    {
        using namespace Core;

        auto& dom = memoryDomains[0];
        dom.zeroPage = EarlyPageAlloc();
        uintptr_t vaddrAlloc = EarlyMmuBegin(EarlyPageAlloc) + GetNextKaslrSlide();

        //1. map the pfndb, or 'PageInfo DB' in northport parlance
        const uintptr_t infoDbBase = vaddrAlloc;
        dom.infoDb = reinterpret_cast<PageInfo*>(infoDbBase);

        Loader::MemmapEntry mmapStore[IngestChunkSize];
        size_t mmapAccum = 0;
        while (true)
        {
            const size_t count = GetUsableMemmap(mmapStore, mmapAccum);
            mmapAccum += count;

            for (size_t i = 0; i < count; i++)
            {
                const auto entry = mmapStore[i];
                if (mmapAccum == count && i == 0)
                    dom.physOffset = entry.base; //this relies on memory map being sorted by base address

                const size_t infoBase = (entry.base - dom.physOffset) >> PfnShift();
                const size_t infoLength = entry.length >> PfnShift();

                uintptr_t mapBase = infoDbBase + AlignDownPage(infoBase * sizeof(PageInfo));
                const uintptr_t mapTop = infoDbBase + AlignUpPage((infoBase + infoLength) * sizeof(PageInfo));
                while (mapBase < vaddrAlloc)
                    mapBase += PageSize();
                while (mapBase <= mapTop)
                {
                    EarlyMmuMap(EarlyPageAlloc, mapBase, EarlyPageAlloc(), MmuFlag::Write);
                    mapBase += PageSize();
                }

                vaddrAlloc = mapTop;
            }

            if (count < IngestChunkSize)
                break;
        }
        Log("PageInfo database at 0x%tx-0x%tx", LogLevel::Verbose,
            infoDbBase, vaddrAlloc);
        vaddrAlloc += GetNextKaslrSlide();

        //2. populate intermediate tables by mapping zero page to all cache space
        const size_t pmaEntries = GetConfigNumber("kernel.pma.cache_entries", DefaultPmaEntries);
        dom.pmaBase = vaddrAlloc;
        vaddrAlloc += (pmaEntries << PfnShift()) + GetNextKaslrSlide();

        for (size_t i = 0; i < pmaEntries; i++)
            EarlyMmuMap(EarlyPageAlloc, dom.pmaBase + (i << PfnShift()), dom.zeroPage, {});
        Log("PMA cache space at 0x%tx, entries=%zu", LogLevel::Verbose, dom.pmaBase, pmaEntries);

        //3. setup memory for pma cache metadata
        const size_t pmaSlotsSize = pmaEntries * sizeof(Core::PmaCache::Slot);
        for (size_t i = 0; i < pmaSlotsSize; i += PageSize())
            EarlyMmuMap(EarlyPageAlloc, vaddrAlloc + (i << PfnShift()), EarlyPageAlloc(), MmuFlag::Write);

        InitPmaCache({ reinterpret_cast<PmaCache::Slot*>(vaddrAlloc), pmaEntries }, dom.zeroPage);
        Log("PMA cache slots at 0x%tx", LogLevel::Verbose, vaddrAlloc);
        vaddrAlloc += AlignUpPage(pmaSlotsSize) + GetNextKaslrSlide();

        //3. setup memory for per-cpu data
        const size_t perCpuSize = smpInfo.localsStride * smpInfo.cpuCount;
        smpInfo.localsBase = vaddrAlloc;
        vaddrAlloc += AlignUpPage(perCpuSize) + GetNextKaslrSlide();

        for (size_t i = 0; i < perCpuSize; i += PageSize())
            EarlyMmuMap(EarlyPageAlloc, vaddrAlloc + (i << PfnShift()), EarlyPageAlloc(), MmuFlag::Write);
        Log("PerCpu data at 0x%tx, stride=0x%tx (total=0x%tx)", LogLevel::Verbose,
            smpInfo.localsBase, smpInfo.localsStride, perCpuSize);

        //4. map memory for kernel idle stacks. Note that the BSP's idle stack is the initial stack
        //used by the kernel entry (this very code).
        smpInfo.idleStacksBase = vaddrAlloc;
        for (size_t i = 0; i < smpInfo.cpuCount; i++)
        {
            //TODO: special handling for BSP
            for (size_t j = 0; j < KernelStackSize(); j += PageSize())
                EarlyMmuMap(EarlyPageAlloc, vaddrAlloc + (j + 1) * PageSize(), EarlyPageAlloc(), MmuFlag::Write);
            vaddrAlloc += PageSize() + KernelStackSize();
        }
        vaddrAlloc += PageSize() + GetNextKaslrSlide(); //guard page after last stack
        Log("Idle stacks beginning at 0x%tx, 0x%tx (+ guard page) per cpu", LogLevel::Verbose,
            smpInfo.idleStacksBase, KernelStackSize());

        //5. map the kernel image
        const uintptr_t virtBase = reinterpret_cast<uintptr_t>(KERNEL_BLOB_BEGIN);
        const uintptr_t physBase = Loader::GetKernelPhysAddr();

        for (char* i = sl::AlignDown(KERNEL_TEXT_BEGIN, PageSize()); i < KERNEL_TEXT_END; i += PageSize())
            EarlyMmuMap(EarlyPageAlloc, (uintptr_t)i, (uintptr_t)i - virtBase + physBase, MmuFlag::Execute);
        for (char* i = sl::AlignDown(KERNEL_RODATA_BEGIN, PageSize()); i < KERNEL_RODATA_END; i += PageSize())
            EarlyMmuMap(EarlyPageAlloc, (uintptr_t)i, (uintptr_t)i - virtBase + physBase, {});
        for (char* i = sl::AlignDown(KERNEL_DATA_BEGIN, PageSize()); i < KERNEL_DATA_END; i += PageSize())
            EarlyMmuMap(EarlyPageAlloc, (uintptr_t)i, (uintptr_t)i - virtBase + physBase, MmuFlag::Write);

        Log("Kernel image at: text=%p-%p, rodata=%p-%p, rwdata=%p-%p", LogLevel::Verbose,
            KERNEL_TEXT_BEGIN, KERNEL_TEXT_END, KERNEL_RODATA_BEGIN, KERNEL_RODATA_END,
            KERNEL_DATA_BEGIN, KERNEL_DATA_END);

        //6. call arch-layer hook to allow it to join-in on the fun of making
        //the initial memory space.
        ArchMappingEntry(EarlyPageAlloc, vaddrAlloc);
        EarlyMmuEnd();

        size_t totalRam = 0;
        for (size_t i = 0; i < memoryDomains.Size(); i++)
        {
            mmapAccum = 0;
            while (true)
            {
                const size_t count = GetUsableMemmap(mmapStore, mmapAccum);
                mmapAccum += count;
                //TODO: filter memmap based on addresses associated with this domain
                //TODO: also filter based on early bump allocator head
                IngestMemory(dom, { mmapStore, count });

                if (count < IngestChunkSize)
                    break;

                const auto conv = sl::ConvertUnits(dom.freeLists.pageCount);
                Log("Domain %zu usable physical memory: 0x%zx (%zu.%zu %sB)", LogLevel::Info,
                    i, dom.freeLists.pageCount, conv.major, conv.minor, conv.prefix);
                totalRam += dom.freeLists.pageCount;
            }
        }

        const auto conv = sl::ConvertUnits(totalRam);
        Log("Total usable system memory: 0x%zx pages (%zu.%zu %sB)", LogLevel::Info,
            totalRam >> PfnShift(), conv.major, conv.minor, conv.prefix);

        //TODO: fixup pfndb entries for pages used so far
        return vaddrAlloc;
    }

    static void SetupMemoryDomains()
    {} //TODO:

    static void BootOtherCpus()
    {
        PalCpu cpus[IngestChunkSize];
        size_t accum = 0;

        while (true)
        {
            const size_t count = PalGetCpus(cpus, accum);

            for (size_t i = 0; i < count; i++)
            {
                if (cpus[i].isBsp)
                    PerCoreEntry(accum + i);
                else
                    PalBootCpu(cpus[i]);
            }

            accum += count;
            if (count < IngestChunkSize)
                break;
        }
    }
    
    extern "C" void KernelEntry()
    {
        using namespace Core;

        ArchEarlyEntry();
        PalEarlyEntry();
#if NPK_HAS_KASAN
        InitAsan();
#endif

        Log("Northport kernel started: v%zu.%zu.%zu for %s, compiled by %s from commit %s%s",
            LogLevel::Info, versionMajor, versionMinor, versionRev, targetArchStr, 
            toolchainUsed, gitCommitShortHash, gitCommitDirty ? "-dirty" : "");

        const size_t globalCtorCount = ((uintptr_t)&INIT_ARRAY_END - (uintptr_t)&INIT_ARRAY_BEGIN) / sizeof(void*);
        for (size_t i = 0; i < globalCtorCount; i++)
            INIT_ARRAY_BEGIN[i]();
        Log("Ran %zu global constructors.", LogLevel::Verbose, globalCtorCount);

        InitConfigStore(Loader::GetCommandLine());
        Loader::Validate();
        //Core::InitGlobalLogging();

        const size_t mailCount = Core::GetConfigNumber("kernel.smp.mail_count", DefaultMailCount);
        smpInfo.cpuCount = PalGetCpus({}, 0);
        smpInfo.localsStride = sl::AlignUp(ArchGetPerCpuSize(), SL_DEFAULT_ALIGNMENT);

        smpInfo.offsets.mailbox = smpInfo.localsStride;
        smpInfo.localsStride += sl::AlignUp(sizeof(MailboxControl), SL_DEFAULT_ALIGNMENT)
            + sl::AlignUp(sizeof(MailboxEntry) * mailCount, SL_DEFAULT_ALIGNMENT);

        const uintptr_t fixedMapTop = SetupMemoryDomain0();
        MmuActivate(memoryDomains[0].kernelSpace, true);
        //NOTE: at this point we have no further access to bootloader data.
        
        //setup per-cpu mailbox lists: all entries start as free
        for (size_t i = 0; i < smpInfo.cpuCount; i++)
        {
            const uintptr_t controlAddr = smpInfo.localsBase + (smpInfo.localsStride * i)
                + smpInfo.offsets.mailbox;
            auto mailControl = reinterpret_cast<MailboxControl*>(controlAddr);
            auto mailEntries = reinterpret_cast<MailboxEntry*>(
                sl::AlignUp(controlAddr, SL_DEFAULT_ALIGNMENT) + sizeof(MailboxControl));

            sl::SpinLock scopeLock(mailControl->lock);
            for (size_t j = 0; j < mailCount; j++)
                mailControl->free.PushBack(&mailEntries[j]);
        }
        
        Core::Rsdp(Loader::GetRsdp());
        //TODO: FDT init
        SetupMemoryDomains();
        
        //TODO: memory stack: wired heap, vmm
        ArchLateEntry();
        PalLateEntry();
        BootOtherCpus();

        //TODO: initial threads (init thread, vm daemon, page zeroing thread)
        ASSERT_UNREACHABLE();
    }
}
