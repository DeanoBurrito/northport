#include <Core.hpp>
#include <Vm.hpp>

namespace Npk
{
    constexpr HeapTag TlbHeapTag = NPK_MAKE_HEAP_TAG("Tlbs");

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
        TlbFlushRequest request;
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

    using DeferredFreePageList = sl::FwdList<DeferredFreeList,
        &DeferredFreeList::queueHook>;

    struct TlbSyncBlock
    {
        //written by remote cpus
        alignas(HwGetStaticCacheLineSize())
        sl::Atomic<bool> pending;
        sl::Atomic<bool> flushAll;
        sl::Atomic<bool> flushGlobals;
        sl::Span<SyncSlot> slots;

        //written by the local cpu
        alignas(HwGetStaticCacheLineSize())
        IplSpinLock<Ipl::Dpc> reclaimLock;
        DeferredFreePageList reclaimList;
        PageList reclaimAccum;
        size_t reclaimAccumCount;
        TlbSyncStats stats;

        //written by any cpu (whichever runs the page free function)
        alignas(HwGetStaticCacheLineSize())
        sl::Atomic<size_t> freedPages;
    };

    //TODO: make these NODE_LOCAL variables.
    static sl::Span<TlbSyncBlock> syncBlocks;
    static bool broadcastTlbs = false;

    static size_t RequestPageCount(const TlbFlushRequest& request)
    {
        if (!IsRangedFlush(request))
            return 0;

        return request.length >> PfnShift();
    }

    static bool TouchesGlobals(const TlbFlushRequest& request)
    {
        return request.target == TlbTarget::Globals
            || request.target == TlbTarget::GlobalsRange
            || request.target == TlbTarget::Everything;
    }

    static bool TargetsOneSpace(const TlbFlushRequest& request)
    {
        return request.target == TlbTarget::Space
            || request.target == TlbTarget::SpaceRange;
    }

    static CpuId DomainCpuId(size_t who)
    {
        return static_cast<CpuId>(who) + MySystemDomain().smpBase;
    }

    static sl::Opt<CpuId> ActorCpu(EbrDomain& dom, size_t who)
    {
        (void)dom;

        return DomainCpuId(who);
    }

    static void NotifyCpu(EbrDomain& dom, size_t who)
    {
        (void)dom;

        HwSendIpi(DomainCpuId(who));

        if (syncBlocks.Empty())
            return;

        auto prevIpl = RaiseIpl(Ipl::Dpc);

        auto& localTlb = syncBlocks[MyRelativeCoreId()];
        localTlb.stats.Add(TlbSyncStat::IpisSent, 1);

        LowerIpl(prevIpl);
    }

    static void SubmitDeferredFree(DeferredFreeList* list)
    {
        auto result = EbrCall(MySystemDomain().tlb, MyRelativeCoreId(),
            &list->item);
        if (result != NpkStatus::Success)
            NPK_UNEXPECTED_STATUS(result, LogLevel::Error);
    }

    static void DeferredFreeCallback(EbrItem* item)
    {
        auto* list = reinterpret_cast<DeferredFreeList*>(item);
        auto& owner = *list->owner;

        owner.freedPages.Add(list->pageCount, sl::Relaxed);
        FreePageList(list->pages);

        owner.reclaimLock.Lock();
        list->pages.Splice(owner.reclaimAccum);
        const size_t reparked = owner.reclaimAccumCount;
        owner.reclaimAccumCount = 0;

        list->pageCount = reparked;
        if (reparked == 0)
            owner.reclaimList.PushFront(list);
        owner.reclaimLock.Unlock();

        if (reparked != 0)
            SubmitDeferredFree(list);
    }

    static bool IsFirstSlotWithTag(const sl::Span<SyncSlot>& slots, size_t i)
    {
        for (size_t j = 0; j < i; j++)
        {
            const auto& other = slots[j];

            if (other.state.Load(sl::Relaxed) != SlotState::Draining)
                continue;
            if (!TargetsOneSpace(other.request))
                continue;

            if (other.request.asid == slots[i].request.asid)
                return false;
        }

        return true;
    }

    static void MaintainLocalTlb(TlbSyncBlock& localTlb)
    {
        localTlb.pending.Exchange(false, sl::AcqRel);
        const bool overflowed = localTlb.flushAll.Exchange(false, sl::AcqRel);
        bool anyGlobals = localTlb.flushGlobals.Exchange(false, sl::AcqRel);

        size_t pageCount = 0;
        size_t drainedSlots = 0;
        size_t drainedPages = 0;
        size_t tagCount = 0;
        bool anyBulk = false;

        for (size_t i = 0; i < localTlb.slots.Size(); i++)
        {
            auto& slot = localTlb.slots[i];
            if (slot.state.Load(sl::Acquire) != SlotState::Active)
                continue;

            slot.state.Store(SlotState::Draining, sl::Relaxed);

            pageCount += RequestPageCount(slot.request);

            switch (slot.request.target)
            {
            case TlbTarget::Globals:
            case TlbTarget::GlobalsRange:
                anyGlobals = true;
                break;
            case TlbTarget::Space:
            case TlbTarget::SpaceRange:
                if (IsFirstSlotWithTag(localTlb.slots, i))
                    tagCount++;
                break;
            case TlbTarget::AllSpaces:
                anyBulk = true;
                break;
            case TlbTarget::Everything:
                anyBulk = true;
                anyGlobals = true;
                break;
            }
        }

        const bool collapse = overflowed
            || pageCount > HwGetTlbFlushThreshold();
        const bool flushAllSpaces = collapse && (overflowed || anyBulk
            || tagCount > HwGetTlbTagFlushThreshold());

        size_t globalFlushes = 0;

        if (flushAllSpaces)
            HwFlushTlb(FlushAllSpaces());
        if (collapse && anyGlobals)
        {
            HwFlushTlb(FlushGlobals());
            globalFlushes++;
        }

        if (collapse && !flushAllSpaces)
        {
            for (size_t i = 0; i < localTlb.slots.Size(); i++)
            {
                auto& slot = localTlb.slots[i];
                if (slot.state.Load(sl::Relaxed) != SlotState::Draining)
                    continue;
                if (!TargetsOneSpace(slot.request))
                    continue;

                if (IsFirstSlotWithTag(localTlb.slots, i))
                    HwFlushTlb(FlushSpace(slot.request.asid));
            }
        }

        for (size_t i = 0; i < localTlb.slots.Size(); i++)
        {
            auto& slot = localTlb.slots[i];
            if (slot.state.Load(sl::Relaxed) != SlotState::Draining)
                continue;

            drainedSlots++;
            drainedPages += RequestPageCount(slot.request);

            if (!collapse)
            {
                HwFlushTlb(slot.request);

                if (TouchesGlobals(slot.request))
                    globalFlushes++;
            }
            //else: covered by one of the flushes above.

            slot.state.Store(SlotState::Free, sl::Release);
        }

        localTlb.stats.Add(TlbSyncStat::Drains, 1);
        localTlb.stats.Add(TlbSyncStat::DrainedSlots, drainedSlots);
        localTlb.stats.Add(TlbSyncStat::DrainedPages, drainedPages);
        localTlb.stats.Add(TlbSyncStat::TagCollapses, collapse ? 1 : 0);
        localTlb.stats.Add(TlbSyncStat::AllSpaceFlushes,
            flushAllSpaces ? 1 : 0);
        localTlb.stats.Add(TlbSyncStat::GlobalFlushes, globalFlushes);
    }

    static void FreeWired(void* ptr, size_t length)
    {
        auto result = PoolFreeWired(ptr, length, TlbHeapTag);
        if (result != NpkStatus::Success)
            NPK_UNEXPECTED_STATUS(result, LogLevel::Error);
    }

    NpkStatus InitTlbSync()
    {
        if (HwHasBroadcastInvalidate())
        {
            broadcastTlbs = true;
            Log("Tlb sync using hardware broadcast invalidation.",
                LogLevel::Info);

            return NpkStatus::Success;
        }

        auto& dom = MySystemDomain();
        const size_t cpuCount = dom.smpControls.Size();

        const size_t slotsPerCpu = ReadConfigUint("npk.tlb.sync_slots",
            DefaultSyncSlotCount);
        const size_t deferItemsPerCpu = ReadConfigUint("npk.tlb.defer_items",
            DefaultDeferredFreeCount);
        Log("Initializing software TLB sync: slots=%zu, deferItems=%zu",
            LogLevel::Info, slotsPerCpu, deferItemsPerCpu);

        if ((slotsPerCpu * sizeof(SyncSlot)) % HwGetStaticCacheLineSize() != 0)
        {
            Log("Tlb sync slots=%zu leaves per-cpu slot arrays unaligned to a"
                " cache line, adjacent cpus will false share when depositing.",
                LogLevel::Warning, slotsPerCpu);
        }

        const size_t blocksLen = cpuCount * sizeof(TlbSyncBlock) +
            alignof(TlbSyncBlock);
        void* blocksAlloc = PoolAllocWired(blocksLen, TlbHeapTag);
        if (blocksAlloc == nullptr)
            return NpkStatus::Shortage;

        auto* blocks = static_cast<TlbSyncBlock*>(
            sl::AlignUp(blocksAlloc, alignof(TlbSyncBlock)));
        for (size_t i = 0; i < cpuCount; i++)
            new (&blocks[i]) TlbSyncBlock {};

        const size_t slotsLen = cpuCount * slotsPerCpu * sizeof(SyncSlot)
            + HwGetStaticCacheLineSize();
        void* slotsAlloc = PoolAllocWired(slotsLen, TlbHeapTag);
        if (slotsAlloc == nullptr)
        {
            FreeWired(blocksAlloc, blocksLen);
            return NpkStatus::Shortage;
        }

        auto* slots = static_cast<SyncSlot*>(
            sl::AlignUp(slotsAlloc, HwGetStaticCacheLineSize()));
        for (size_t i = 0; i < cpuCount * slotsPerCpu; i++)
            new (&slots[i]) SyncSlot {};

        const size_t deferLen = cpuCount * deferItemsPerCpu
            * sizeof(DeferredFreeList) + HwGetStaticCacheLineSize();
        void* defersAlloc = PoolAllocWired(deferLen, TlbHeapTag);
        if (defersAlloc == nullptr)
        {
            FreeWired(slotsAlloc, slotsLen);
            FreeWired(blocksAlloc, blocksLen);
            return NpkStatus::Shortage;
        }
        auto* defers = static_cast<DeferredFreeList*>(
            sl::AlignUp(defersAlloc, HwGetStaticCacheLineSize()));

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

        auto result = ResetEbrDomain(dom.tlb, cpuCount, NotifyCpu, ActorCpu);
        if (result != NpkStatus::Success)
        {
            FreeWired(defersAlloc, deferLen);
            FreeWired(slotsAlloc, slotsLen);
            FreeWired(blocksAlloc, blocksLen);
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

        stats.Set(TlbSyncStat::FreedPages,
            localTlb.freedPages.Load(sl::Relaxed));

        return NpkStatus::Success;
    }

    static bool DepositToBlock(TlbSyncBlock& target,
        const TlbFlushRequest& request)
    {
        bool foundSlot = false;

        for (size_t j = 0; j < target.slots.Size(); j++)
        {
            auto& slot = target.slots[j];

            auto expected = SlotState::Free;
            auto desired = SlotState::Reserved;
            if (!slot.state.CompareExchange(expected, desired, sl::AcqRel))
                continue;

            slot.request = request;
            slot.state.Store(SlotState::Active, sl::Release);

            foundSlot = true;
            break;
        }

        if (!foundSlot)
        {
            if (TouchesGlobals(request))
                target.flushGlobals.Store(true, sl::Release);

            target.flushAll.Store(true, sl::Release);
        }

        target.pending.Store(true, sl::Release);

        return foundSlot;
    }

    static bool DepositTooEarly(const TlbFlushRequest& request)
    {
        if (!syncBlocks.Empty())
            return false;

        HwFlushTlb(request);

        return true;
    }

    void TlbSyncDeposit(const CpuBitset* targets,
        const TlbFlushRequest& request)
    {
        if (broadcastTlbs)
        {
            HwInvalidateTlbs(request);
            return;
        }

        if (DepositTooEarly(request))
            return;

        NPK_ASSERT(CurrentIpl() >= Ipl::Dpc);

        const size_t self = MyRelativeCoreId();
        auto& localTlb = syncBlocks[self];
        size_t overflows = 0;
        size_t slotDeposits = 0;

        if (targets == nullptr || targets->Has(self))
            HwFlushTlb(request);

        for (size_t i = 0; i < syncBlocks.Size(); i++)
        {
            if (i == self)
                continue;
            if (targets != nullptr && !targets->Has(i))
                continue;

            if (DepositToBlock(syncBlocks[i], request))
                slotDeposits++;
            else
                overflows++;
        }

        localTlb.stats.Add(TlbSyncStat::Deposits, 1);
        localTlb.stats.Add(TlbSyncStat::DepositOverflows, overflows);
        localTlb.stats.Add(TlbSyncStat::SlotDeposits, slotDeposits);
    }

    void TlbSyncDepositOne(size_t cpu, const TlbFlushRequest& request)
    {
        if (broadcastTlbs)
        {
            HwInvalidateTlbs(request);
            return;
        }

        if (DepositTooEarly(request))
            return;

        NPK_ASSERT(CurrentIpl() >= Ipl::Dpc);
        NPK_ASSERT(cpu < syncBlocks.Size());

        const size_t self = MyRelativeCoreId();
        auto& localTlb = syncBlocks[self];

        localTlb.stats.Add(TlbSyncStat::Deposits, 1);

        if (cpu == self)
        {
            HwFlushTlb(request);
            return;
        }

        if (DepositToBlock(syncBlocks[cpu], request))
            localTlb.stats.Add(TlbSyncStat::SlotDeposits, 1);
        else
            localTlb.stats.Add(TlbSyncStat::DepositOverflows, 1);
    }

    void TlbSyncReclaim(PageList& pages)
    {
        if (pages.Empty())
            return;

        if (broadcastTlbs)
        {
            HwSyncTlbs();
            FreePageList(pages);

            return;
        }

        NPK_ASSERT(CurrentIpl() >= Ipl::Dpc);
        NPK_ASSERT(!syncBlocks.Empty());

        auto& localTlb = syncBlocks[MyRelativeCoreId()];

        size_t newPages = 0;
        for (auto it = pages.Begin(); it != pages.End(); ++it)
            newPages++;

        DeferredFreeList* list = nullptr;
        size_t accumPages = 0;

        localTlb.reclaimLock.Lock();
        if (localTlb.reclaimList.Empty())
        {
            localTlb.reclaimAccum.Splice(pages);
            localTlb.reclaimAccumCount += newPages;
        }
        else
        {
            list = localTlb.reclaimList.PopFront();

            list->pages.Splice(localTlb.reclaimAccum);
            accumPages = localTlb.reclaimAccumCount;
            localTlb.reclaimAccumCount = 0;
        }
        localTlb.reclaimLock.Unlock();

        localTlb.stats.Add(TlbSyncStat::DeferredPages, newPages);
        localTlb.stats.Add(TlbSyncStat::DeferredFreeOverflows,
            list == nullptr ? 1 : 0);

        if (list == nullptr)
            return;

        list->pages.Splice(pages);
        list->pageCount = accumPages + newPages;

        SubmitDeferredFree(list);
    }

    void TlbSyncWait()
    {
        if (broadcastTlbs)
            return HwSyncTlbs();

        NPK_ASSERT(!syncBlocks.Empty());

        EbrSyncExpedited(MySystemDomain().tlb, MyRelativeCoreId());
    }

    void TlbSyncQuiesce()
    {
        AssertIpl(Ipl::Tlb);

        if (broadcastTlbs)
            return;
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

        HwFlushTlb(FlushEverything());
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
