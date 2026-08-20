#include <hardware/common/mmu/TlbSync.hpp>
#include <Core.hpp>
#include <Vm.hpp>

namespace Npk
{
    constexpr HeapTag TlbHeapTag = NPK_MAKE_HEAP_TAG("Tlbs");

    constexpr size_t FlushWholeAsid = ~static_cast<size_t>(0);
    constexpr size_t DefaultSyncSlotCount = 64;
    constexpr size_t DefaultDeferredFreeCount = 8;

    enum class SlotState
    {
        Free,
        Reserved,
        Active,
        Draining,
    };

    struct SyncSlot
    {
        sl::Atomic<SlotState> state;
        Asid asid;
        uintptr_t base;
        size_t length;
    };

    struct TlbSyncBlock;

    struct DeferredFreeList
    {
        EbrItem item;
        sl::FwdListHook queueHook;
        PageList pages;
        size_t pageCount;
        TlbSyncBlock* owner;
    };
    static_assert(offsetof(DeferredFreeList, item) == 0);
    //see DeferredFreeCallback() for why this assert is here.

    using DeferredFreePageList = sl::FwdList<DeferredFreeList, 
        &DeferredFreeList::queueHook>;

    struct TlbSyncBlock
    {
        alignas(HwGetStaticCacheLineSize())
        sl::Atomic<bool> pending;
        sl::Atomic<bool> flushAll;

        alignas(HwGetStaticCacheLineSize())
        IplSpinLock<Ipl::Dpc> reclaimLock;
        DeferredFreePageList reclaimList;
        PageList reclaimAccum;
        sl::Span<SyncSlot> slots;
        TlbSyncStats stats;

        sl::Atomic<size_t> freedPages;
    };

    //TODO: make these NODE_LOCAL variables.
    static sl::Span<TlbSyncBlock> syncBlocks;

    static void DoLocalFlush(const SyncSlot& slot)
    {
        if (slot.length == FlushWholeAsid)
            HwFlushTlbAll(slot.asid);
        else
            HwFlushTlb(slot.asid, slot.base, slot.length);
    }

    static void NotifyCpu(EbrDomain& dom, size_t who)
    {
        (void)dom;

        HwSendIpi(static_cast<CpuId>(who) + MySystemDomain().smpBase);

        if (syncBlocks.Empty())
            return;

        //raise IPL as this runs at passive level and we dont want to migrate
        //mid write to a statistic.
        auto prevIpl = RaiseIpl(Ipl::Dpc);

        auto& localTlb = syncBlocks[MyRelativeCoreId()];
        localTlb.stats.Add(TlbSyncStat::IpisSent, 1);

        LowerIpl(prevIpl);
    }

    static void DeferredFreeCallback(EbrItem* item)
    {
        auto* pages = reinterpret_cast<DeferredFreeList*>(item);
        auto& owner = *pages->owner;

        owner.freedPages.Add(pages->pageCount, sl::Relaxed);
        FreePageList(pages->pages);

        size_t reparkCount = 0;

        owner.reclaimLock.Lock();
        while (!owner.reclaimAccum.Empty())
        {
            pages->pages.PushBack(owner.reclaimAccum.PopFront());
            reparkCount++;
        }

        pages->pageCount = reparkCount;
        if (reparkCount == 0)
            owner.reclaimList.PushFront(pages);
        owner.reclaimLock.Unlock();

        if (reparkCount == 0)
            return;

        auto result = EbrCall(MySystemDomain().tlb, MyRelativeCoreId(), item);
        if (result != NpkStatus::Success)
            NPK_UNEXPECTED_STATUS(result, LogLevel::Error);
    }

    static void MaintainLocalTlb(TlbSyncBlock& localTlb)
    {
        localTlb.pending.Exchange(false, sl::AcqRel);

        bool flushAll = localTlb.flushAll.Exchange(false, sl::AcqRel);
        size_t flushCount = 0;
        size_t drainedSlots = 0;
        size_t drainedPages = 0;

        for (size_t i = 0; i < localTlb.slots.Size(); i++)
        {
            auto& slot = localTlb.slots[i];
            if (slot.state.Load(sl::Acquire) != SlotState::Active)
                continue;

            slot.state.Store(SlotState::Draining, sl::Relaxed);

            if (slot.length != FlushWholeAsid)
                flushCount += slot.length >> PfnShift();
        }

        flushAll |= flushCount > HwGetTlbFlushThreshold();

        if (flushAll)
            HwFlushTlbAll(AsidNone);

        for (size_t i = 0; i < localTlb.slots.Size(); i++)
        {
            auto& slot = localTlb.slots[i];
            if (slot.state.Load(sl::Relaxed) != SlotState::Draining)
                continue;

            if (!flushAll)
                DoLocalFlush(slot);

            drainedSlots++;
            if (slot.length != FlushWholeAsid)
                drainedPages += slot.length >> PfnShift();

            slot.state.Store(SlotState::Free, sl::Release);
        }

        localTlb.stats.Add(TlbSyncStat::Drains, 1);
        localTlb.stats.Add(TlbSyncStat::DrainedSlots, drainedSlots);
        localTlb.stats.Add(TlbSyncStat::DrainedPages, drainedPages);
    }

    NpkStatus InitSoftwareTlbSync()
    {
        auto& dom = MySystemDomain();
        const size_t cpuCount = dom.smpControls.Size();

        //TODO: warn if this value would result in false sharing of slot structs
        const size_t slotsPerCpu = ReadConfigUint("npk.tlb.sync_slots",
            DefaultSyncSlotCount);
        const size_t deferItemsPerCpu = ReadConfigUint("npk.tlb.defer_items", 
            DefaultDeferredFreeCount);
        Log("Initializing software TLB sync: slots=%zu, deferItems=%zu", 
            LogLevel::Info, slotsPerCpu, deferItemsPerCpu);

        const size_t blocksLen = cpuCount * sizeof(TlbSyncBlock) +
            alignof(TlbSyncBlock);
        void* ptr = PoolAllocWired(blocksLen, TlbHeapTag);
        if (ptr == nullptr)
            return NpkStatus::Shortage;

        auto* blocks = static_cast<TlbSyncBlock*>(
            sl::AlignUp(ptr, alignof(TlbSyncBlock)));
        for (size_t i = 0; i < cpuCount; i++)
            new (&blocks[i]) TlbSyncBlock {};

        const size_t slotsLen = cpuCount * slotsPerCpu * sizeof(SyncSlot) 
            + HwGetStaticCacheLineSize();
        ptr = PoolAllocWired(slotsLen, TlbHeapTag);
        if (ptr == nullptr)
        {
            auto result = PoolFreeWired(blocks, blocksLen, TlbHeapTag);
            if (result != NpkStatus::Success)
                NPK_UNEXPECTED_STATUS(result, LogLevel::Error);

            return NpkStatus::Shortage;
        }

        auto* slots = static_cast<SyncSlot*>(
            sl::AlignUp(ptr, alignof(SyncSlot)));
        for (size_t i = 0; i < cpuCount * slotsPerCpu; i++)
            new (&slots[i]) SyncSlot {};

        const size_t deferLen = cpuCount * deferItemsPerCpu
            * sizeof(DeferredFreeList) + HwGetStaticCacheLineSize();
        ptr = PoolAllocWired(deferLen, TlbHeapTag);
        if (ptr == nullptr)
        {
            auto result = PoolFreeWired(slots, slotsLen, TlbHeapTag);
            if (result != NpkStatus::Success)
                NPK_UNEXPECTED_STATUS(result, LogLevel::Error);

            result = PoolFreeWired(blocks, blocksLen, TlbHeapTag);
            if (result != NpkStatus::Success)
                NPK_UNEXPECTED_STATUS(result, LogLevel::Error);

            return NpkStatus::Shortage;
        }
        auto* defers = static_cast<DeferredFreeList*>(
            sl::AlignUp(ptr, alignof(DeferredFreeList)));

        for (size_t i = 0; i < cpuCount; i++)
        {
            auto& block = blocks[i];

            block.slots = { &slots[i * slotsPerCpu], slotsPerCpu };

            block.reclaimLock.Lock();
            for (size_t j = 0; j < deferItemsPerCpu; j++)
            {
                auto& item = defers[deferItemsPerCpu * i + j];
                new (&item) DeferredFreeList {};

                item.item.callback = DeferredFreeCallback;
                item.owner = &block;
                
                block.reclaimList.PushBack(&item);
            }
            block.reclaimLock.Unlock();
        }

        auto result = ResetEbrDomain(dom.tlb, cpuCount, NotifyCpu);
        if (result != NpkStatus::Success)
        {
            auto innerResult = PoolFreeWired(defers, deferLen, TlbHeapTag);
            if (innerResult != NpkStatus::Success)
                NPK_UNEXPECTED_STATUS(innerResult, LogLevel::Error);

            innerResult = PoolFreeWired(slots, slotsLen, TlbHeapTag);
            if (innerResult != NpkStatus::Success)
                NPK_UNEXPECTED_STATUS(innerResult, LogLevel::Error);

            innerResult = PoolFreeWired(blocks, blocksLen, TlbHeapTag);
            if (innerResult != NpkStatus::Success)
                NPK_UNEXPECTED_STATUS(innerResult, LogLevel::Error);

            return result;
        }

        syncBlocks = { blocks, cpuCount };
        Log("Software TLB sync init complete.", LogLevel::Trace);

        return NpkStatus::Success;
    }

    NpkStatus GetTlbSyncStats(TlbSyncStats& stats)
    {
        if (syncBlocks.Empty())
            return NpkStatus::NotAvailable;

        auto& localTlb = syncBlocks[MyRelativeCoreId()];

        if (!localTlb.stats.Copy(stats))
            return NpkStatus::Busy;

        //freedPages is tracked separately because its written from remote cpus,
        //so we fold the real value back in to the stat block here.
        stats.Set(TlbSyncStat::FreedPages,
            localTlb.freedPages.Load(sl::Relaxed));

        return NpkStatus::Success;
    }

    void TlbSyncDeposit(const CpuBitset* targets, Asid asid, uintptr_t vaddr,
        size_t length)
    {
        auto& localTlb = syncBlocks[MyRelativeCoreId()];
        size_t overflows = 0;
        size_t slotDeposits = 0;

        if (targets == nullptr || targets->Has(MyRelativeCoreId()))
        {
            SyncSlot slot
            {
                .state = SlotState::Active,
                .asid = asid,
                .base = vaddr,
                .length = length
            };

            DoLocalFlush(slot);
        }

        for (size_t i = 0; i < syncBlocks.Size(); i++)
        {
            if (i == MyRelativeCoreId())
                continue;
            if (targets != nullptr && !targets->Has(i))
                continue;

            auto& target = syncBlocks[i];

            bool foundSlot = false;
            for (size_t j = 0; j < target.slots.Size(); j++)
            {
                auto& slot = target.slots[j];

                auto expected = SlotState::Free;
                auto desired = SlotState::Reserved;
                if (!slot.state.CompareExchange(expected, desired, sl::AcqRel))
                    continue;

                slot.asid = asid;
                slot.base = vaddr;
                slot.length = length;
                slot.state.Store(SlotState::Active, sl::Release);

                slotDeposits++;
                foundSlot = true;
                break;
            }

            if (!foundSlot)
            {
                target.flushAll.Store(true, sl::Release);
                overflows++;
            }

            target.pending.Store(true, sl::Release);
        }

        localTlb.stats.Add(TlbSyncStat::Deposits, 1);
        localTlb.stats.Add(TlbSyncStat::DepositOverflows, overflows);
        localTlb.stats.Add(TlbSyncStat::SlotDeposits, slotDeposits);
    }

    void TlbSyncDepositAll(const CpuBitset* targets, Asid asid)
    {
        TlbSyncDeposit(targets, asid, 0, FlushWholeAsid);
    }

    void TlbSyncReclaim(PageList& pages)
    {
        NPK_ASSERT(!syncBlocks.Empty());

        if (pages.Empty())
            return;

        auto& localTlb = syncBlocks[MyRelativeCoreId()];
        DeferredFreeList* list = nullptr;

        size_t deferredPages = 0;
        size_t itemPages = 0;
        size_t overflows = 0;

        localTlb.reclaimLock.Lock();
        if (!localTlb.reclaimList.Empty())
        {
            list = localTlb.reclaimList.PopFront();

            while (!localTlb.reclaimAccum.Empty())
            {
                auto page = localTlb.reclaimAccum.PopFront();
                list->pages.PushBack(page);

                itemPages++;
            }
        }
        else
        {
            while (!pages.Empty())
            {
                auto page = pages.PopFront();
                localTlb.reclaimAccum.PushBack(page);

                deferredPages++;
            }

            overflows++;
        }
        localTlb.reclaimLock.Unlock();

        if (list != nullptr)
        {
            while (!pages.Empty())
            {
                list->pages.PushBack(pages.PopFront());

                deferredPages++;
                itemPages++;
            }

            list->pageCount = itemPages;
        }

        localTlb.stats.Add(TlbSyncStat::DeferredPages, deferredPages);
        localTlb.stats.Add(TlbSyncStat::DeferredFreeOverflows, overflows);

        if (list == nullptr)
            return;

        auto result = EbrCall(MySystemDomain().tlb, MyRelativeCoreId(),
            &list->item);
        if (result != NpkStatus::Success)
            NPK_UNEXPECTED_STATUS(result, LogLevel::Error);
    }

    void TlbSyncWait()
    {
        NPK_ASSERT(!syncBlocks.Empty());

        EbrSync(MySystemDomain().tlb, MyRelativeCoreId(), true);
    }

    void TlbSyncQuiesce()
    {
        AssertIpl(Ipl::Tlb);

        if (syncBlocks.Empty())
            return; //too early, only single core is active so just leave.

        auto& localTlb = syncBlocks[MyRelativeCoreId()];
        auto& dom = MySystemDomain().tlb;

        const auto epoch = ObserveEpoch(dom);
        if (localTlb.pending.Load(sl::Acquire))
            MaintainLocalTlb(localTlb);

        NudgeEpoch(dom, MyRelativeCoreId(), epoch);
    }

    void BeginNoTlbSyncEpoch()
    {
        if (syncBlocks.Empty())
            return;

        HwFlushTlbAll(AsidNone);
        EnterNoEpochState(MySystemDomain().tlb, MyRelativeCoreId());
    }

    void EndNoTlbSyncEpoch()
    {
        if (syncBlocks.Empty())
            return;

        auto& localTlb = syncBlocks[MyRelativeCoreId()];

        ExitNoEpochState(MySystemDomain().tlb, MyRelativeCoreId());
        localTlb.pending.Store(true, sl::Release);
    }
}
