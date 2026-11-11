#include <hardware/common/mmu/PageTables.hpp>
#include <Core.hpp>
#include <Vm.hpp>
#include <private/Entry.hpp>
#include <lib/Locks.hpp>

namespace Npk
{
    constexpr HeapTag HwMapTag = NPK_MAKE_HEAP_TAG("Hmap");
    constexpr size_t MaxPteSize = 16;
    constexpr size_t MaxPtPathLevels = 8;

    struct alignas(MaxPteSize) PteOnStack
    {
        uint8_t data[MaxPteSize];
    };

    //this assert is very unscientific, I've just chosen the worst case I can
    //think of (PAE on 32bit x86) and accounted for it.
    static_assert(MaxPteSize >= 2 * sizeof(uintptr_t));

    struct HwMap
    {
        IplSpinLock<Ipl::Dpc> lock;
        Paddr root;

        size_t tlbGen;
        CpuBitset onprocCpus; //cpus with this map active *right now*
        sl::Span<AsidEntry> asids; //one entry per asid domain
    };

    struct PtPageInfo
    {
        sl::FwdListHook freeHook;
        uint16_t validCount;
    };
    static_assert(sizeof(PtPageInfo) <= sizeof(PageInfo));
    static_assert(offsetof(PtPageInfo, freeHook) == 0);
    static_assert(sizeof(PtPageInfo) <= offsetof(PageInfo, flags));

    enum class WalkResult
    {
        Success,
        NoTable,
        BlockMapped,
        NoAccess,
    };

    struct PtWalk
    {
        Paddr tables[MaxPtPathLevels];
        size_t indices[MaxPtPathLevels];
        size_t stopLevel;
        bool builtChain;
    };

    CPU_LOCAL(HwMap*, activeHwMap);

    static size_t PtIndex(const PageTableConfig& conf, uintptr_t vaddr,
        size_t level)
    {
        return (vaddr >> conf.levelShift[level]) & conf.levelMask[level];
    }

    static void* PteAt(const PageTableConfig& conf, const PageAccessRef& ref,
        size_t index)
    {
        return static_cast<char*>(ref.vaddr) + index * conf.pteSize;
    }

    static void DoWritePte(const PageTableConfig& conf, void* dest,
        const void* source)
    {
        if (conf.hasCustomWritePte)
            return WritePte(dest, source);

        switch (conf.pteSize)
        {
        case 4:
        {
            auto* destPtr = static_cast<sl::Atomic<uint32_t>*>(dest);
            auto* srcPtr = static_cast<const sl::Atomic<uint32_t>*>(source);

            const auto value = srcPtr->Load(sl::Acquire);
            destPtr->Store(value, sl::Release);

            break;
        }

        case 8:
        {
            auto* destPtr = static_cast<sl::Atomic<uint64_t>*>(dest);
            auto* srcPtr = static_cast<const sl::Atomic<uint64_t>*>(source);

            const auto value = srcPtr->Load(sl::Acquire);
            destPtr->Store(value, sl::Release);

            break;
        }

        default:
            NPK_UNREACHABLE();
        }
    }

    static void DoExchangePte(const PageTableConfig& conf, void* dest,
        const void* source, void* prev)
    {
        if (conf.hasCustomExchange)
            return ExchangePte(dest, source, prev);

        switch (conf.pteSize)
        {
        case 4:
        {
            auto* ptr = static_cast<sl::Atomic<uint32_t>*>(dest);
            const uint32_t desire = *static_cast<const uint32_t*>(source);

            const auto ret = ptr->Exchange(desire, sl::SeqCst);

            *static_cast<uint32_t*>(prev) = ret;
            break;
        }

        case 8:
        {
            auto* ptr = static_cast<sl::Atomic<uint64_t>*>(dest);
            const uint64_t desire = *static_cast<const uint64_t*>(source);

            const auto ret = ptr->Exchange(desire, sl::SeqCst);

            *static_cast<uint64_t*>(prev) = ret;
            break;
        }

        default:
            NPK_UNREACHABLE();
        }
    }

    static bool DoCompExchangePte(const PageTableConfig& conf, void* dest,
        void* expected, const void* desired)
    {
        if (conf.hasCustomCompareExchange)
            return CompareExchangePte(dest, expected, desired);

        switch (conf.pteSize)
        {
        case 4:
        {
            auto* ptr = static_cast<sl::Atomic<uint32_t>*>(dest);
            uint32_t expect = *static_cast<uint32_t*>(expected);
            const uint32_t desire = *static_cast<const uint32_t*>(desired);

            const auto ret = ptr->CompareExchange(expect, desire, sl::SeqCst);

            *static_cast<uint32_t*>(expected) = expect;

            return ret;
        }

        case 8:
        {
            auto* ptr = static_cast<sl::Atomic<uint64_t>*>(dest);
            uint64_t expect = *static_cast<uint64_t*>(expected);
            const uint64_t desire = *static_cast<const uint64_t*>(desired);

            const auto ret = ptr->CompareExchange(expect, desire, sl::SeqCst);

            *static_cast<uint64_t*>(expected) = expect;

            return ret;
        }

        default:
            NPK_UNREACHABLE();
        }
    }

    static void WriteIntermediatePte(const PageTableConfig& conf, void* pte,
        Paddr child, bool kernel)
    {
        PteOnStack buffer;
        MakeIntermediatePte(buffer.data, child, kernel);

        DoWritePte(conf, pte, buffer.data);
    }

    static void WriteInvalidPte(const PageTableConfig& conf, void* pte)
    {
        PteOnStack buffer;
        MakeInvalidPte(buffer.data);

        DoWritePte(conf, pte, buffer.data);
    }

    static void PublishDirtyBit(Paddr paddr)
    {
        if (!PaddrHasPageInfo(paddr))
            return;

        PageClearFlag(LookupPageInfo(paddr), PageVmFlag::Clean);
    }

    static PtPageInfo& PtMetadata(Paddr paddr)
    {
        auto info = LookupPageInfo(paddr);
        //TODO: this assumes page tables are exactly page sized
        auto meta = reinterpret_cast<PtPageInfo*>(info);

        return *meta;
    }

    static void FreeChain(uintptr_t vaddr, Paddr table, size_t levels)
    {
        const auto& conf = GetPageTableConfig();

        Paddr current = table;
        for (size_t level = levels; current != 0; level--)
        {
            Paddr next = 0;
            if (level != 0)
            {
                auto ref = AccessPage(current);
                NPK_ASSERT(ref.Valid()); //TODO: non-fatal handling

                void* pte = PteAt(conf, ref, PtIndex(conf, vaddr, level));
                if (IsPteValid(pte))
                    next = GetPteAddr(pte);
            }

            FreePage(LookupPageInfo(current));

            if (level == 0)
                break;
            current = next;
        }
    }

    //builds tables for levels from target-1 to 0. Frees the partial chain if
    //not enough memory is available and returns false. Returns the top and
    //leaf addresses and true on success.
    static bool BuildChain(Paddr& outTop, Paddr& outLeaf, uintptr_t vaddr,
        size_t targetLevel, bool global)
    {
        const auto& conf = GetPageTableConfig();

        Paddr child = 0;
        for (size_t level = 0; level < targetLevel; level++)
        {
            auto page = AllocPage(true);
            if (page == nullptr)
            {
                if (level > 0)
                    FreeChain(vaddr, child, level - 1);

                return false;
            }

            const Paddr pagePaddr = LookupPagePaddr(page);
            PtMetadata(pagePaddr).validCount = (level == 0) ? 0 : 1;

            if (level == 0)
                outLeaf = pagePaddr;
            else
            {
                auto ref = AccessPage(pagePaddr);
                auto pte = PteAt(conf, ref, PtIndex(conf, vaddr, level));

                WriteIntermediatePte(conf, pte, child, global);
            }

            child = pagePaddr;
        }

        outTop = child;

        return true;
    }

    static WalkResult Walk(PtWalk& walk, HwMap& map, uintptr_t vaddr, bool alloc)
    {
        const auto& conf = GetPageTableConfig();
        const bool isKernel = &map == HwKernelMap();

        NPK_ASSERT(conf.levelCount <= MaxPtPathLevels);

        walk.stopLevel = 0;
        walk.builtChain = false;

        Paddr currentPt = map.root;
        for (size_t level = conf.levelCount - 1; level > 0; level--)
        {
            const size_t index = PtIndex(conf, vaddr, level);

            walk.tables[level] = currentPt;
            walk.indices[level] = index;
            walk.stopLevel = level;

            auto ref = AccessPage(currentPt);
            if (!ref.Valid())
                return WalkResult::NoAccess;

            void* pte = PteAt(conf, ref, index);

            if (IsPteValid(pte))
            {
                if (IsLeafPte(pte, level))
                    return WalkResult::BlockMapped;

                currentPt = GetPteAddr(pte);
                continue;
            }

            if (!alloc)
                return WalkResult::NoTable;

            Paddr top;
            Paddr leaf;
            if (!BuildChain(top, leaf, vaddr, level, isKernel))
                return WalkResult::NoTable;

            WriteIntermediatePte(conf, pte, top, isKernel);
            PtMetadata(currentPt).validCount++;

            walk.builtChain = true;
            walk.tables[0] = leaf;
            walk.indices[0] = PtIndex(conf, vaddr, 0);

            return WalkResult::Success;
        }

        walk.tables[0] = currentPt;
        walk.indices[0] = PtIndex(conf, vaddr, 0);

        return WalkResult::Success;
    }

    static uintptr_t NextSubtree(uintptr_t vaddr, size_t level)
    {
        const auto& conf = GetPageTableConfig();
        const uintptr_t span = static_cast<uintptr_t>(1)
            << conf.levelShift[level];

        auto next = vaddr + span;
        next &= ~(span - 1);

        return next;
    }

    static void FreeEmptyTables(MapBatch& batch, const PtWalk& walk,
        uintptr_t vaddr)
    {
        NPK_ASSERT(!walk.builtChain);

        const auto& conf = GetPageTableConfig();
        HwMap& map = *batch.map;

        for (size_t level = 0; level + 1 < conf.levelCount; level++)
        {
            const Paddr table = walk.tables[level];

            if (PtMetadata(table).validCount != 0)
                break;

            const Paddr parent = walk.tables[level + 1];
            if (parent == map.root)
                break;

            auto ref = AccessPage(parent);
            NPK_ASSERT(ref.Valid());
            auto pte = PteAt(conf, ref, walk.indices[level + 1]);

            WriteInvalidPte(conf, pte);
            PtMetadata(parent).validCount--;

            const uintptr_t span = (conf.levelMask[level] + 1)
                << conf.levelShift[level];
            MapBatchAddRange(batch, vaddr & ~(span - 1), span);
            MapBatchFreePage(batch, LookupPageInfo(table));
        }
    }

    static bool AllocAsidEntries(HwMap& map)
    {
        const size_t count = AsidDomainCount(SystemAsids());
        NPK_ASSERT(count != 0);

        void* ptr = PoolAllocWired(count * sizeof(AsidEntry), HwMapTag);
        if (ptr == nullptr)
            return false;
        NPK_ASSERT(sl::AlignUp(ptr, alignof(AsidEntry)) == ptr);

        auto* entries = static_cast<AsidEntry*>(ptr);
        for (size_t i = 0; i < count; i++)
            entries[i] = AsidEntry {};

        map.asids = { entries, count };

        return true;
    }

    static void FreeAsidEntries(HwMap& map)
    {
        if (map.asids.Empty())
            return;

        void* base = map.asids.Begin();
        const size_t length = map.asids.SizeBytes();

        auto result = PoolFreeWired(base, length, HwMapTag);
        if (result != NpkStatus::Success)
            NPK_UNEXPECTED_STATUS(result, LogLevel::Error);

        map.asids = {};
    }

    static void CollectTables(MapBatch& batch, Paddr table, size_t level)
    {
        HwMap& map = *batch.map;
        const auto& conf = GetPageTableConfig();

        if (level > 0)
        {
            const bool isSharedRoot = !conf.splitRoot 
                && &map != HwKernelMap() && level == conf.levelCount - 1;

            auto ref = AccessPage(table);
            NPK_ASSERT(ref.Valid());

            for (size_t i = 0; i <= conf.levelMask[level]; i++)
            {
                if (isSharedRoot && i >= conf.kernelFirstIndex
                    && i <= conf.kernelLastIndex)
                    continue;

                void* pte = PteAt(conf, ref, i);

                if (!IsPteValid(pte))
                    continue;
                if (IsLeafPte(pte, level))
                    continue;

                CollectTables(batch, GetPteAddr(pte), level - 1);
            }
        }

        MapBatchFreePage(batch, LookupPageInfo(table));
    }

    //NOTE: assumes the map's lock is held. Records an invalidation only if a
    //bit was actually cleared.
    static MmuPageStates HarvestEntry(MapBatch& batch, void* pte,
        uintptr_t vaddr, MmuPageStates clear)
    {
        const auto state = HarvestPte(pte, vaddr, clear);

        bool rearm = false;
        if (clear.Has(MmuPageState::Accessed))
            rearm |= state.Has(MmuPageState::Accessed);
        if (clear.Has(MmuPageState::Dirty))
            rearm |= state.Has(MmuPageState::Dirty);

        if (rearm)
            MapBatchAddRange(batch, vaddr, PageSize());

        return state;
    }

    static bool DoMinorFault(HwMap& map, uintptr_t vaddr, bool write)
    {
        const auto& conf = GetPageTableConfig();

        PtWalk walk;
        if (Walk(walk, map, vaddr, false) != WalkResult::Success)
            return false;

        auto ref = AccessPage(walk.tables[0]);

        //this is an error but returning `false` here is wrong: its not that
        //the minor fault was unhandled (it may be), its a resource shortage
        //so that should be handled differently. Currently it's late and I
        //to get this committed so I'll just assert this so it fails loudly
        //later on, that'll hopefully remind me to implement a solution here.
        NPK_ASSERT(ref.vaddr);

        void* pte = PteAt(conf, ref, walk.indices[0]);
        if (!IsPteValid(pte))
            return false;

        bool handled = false;
        if (write && !conf.hwDirtyBit && PteIsWriteTrackable(pte))
        {
            SetPteDirty(pte);
            PublishDirtyBit(GetPteAddr(pte));

            handled = true;
        }
        if (!conf.hwAccessedBit && !SetPteAccessed(pte))
            handled = true;

        return handled;
    }

    static bool DepositBatchCoarsely(const MapBatch& batch)
    {
        if (batch.coarse)
            return true;

        size_t pages = 0;
        for (size_t i = 0; i < batch.count; i++)
            pages += batch.storage[i].length >> PfnShift();

        return pages > HwGetTlbFlushThreshold();
    }

    static void CoverKernelMap(const MapBatch& batch)
    {
        if (DepositBatchCoarsely(batch))
        {
            TlbSyncDeposit(nullptr, FlushGlobals());

            return;
        }

        for (size_t i = 0; i < batch.count; i++)
        {
            TlbSyncDeposit(nullptr, FlushGlobalsRange(batch.storage[i].base,
                batch.storage[i].length));
        }
    }

    static void CoverUserMap(HwMap& map, const MapBatch& batch)
    {
        auto& asids = SystemAsids();

        if (batch.count == 0 && !batch.coarse)
            return;

        map.tlbGen++;
        const bool coarse = DepositBatchCoarsely(batch);

        const auto Issue = [&](size_t cpu, bool oneCpu, Asid asid) -> void
        {
            const auto Deposit = [&](const TlbFlushRequest& request) -> void
            {
                if (oneCpu)
                    TlbSyncDepositOne(cpu, request);
                else
                {
                    const auto& domCpus = AsidDomainCpus(asids,
                        AsidDomainOfCpu(asids, cpu));
                    TlbSyncDeposit(&domCpus, request);
                }
            };

            if (coarse)
            {
                Deposit(FlushSpace(asid));

                return;
            }

            for (size_t i = 0; i < batch.count; i++)
            {
                Deposit(FlushSpaceRange(asid, batch.storage[i].base,
                    batch.storage[i].length));
            }
        };

        //1. Shootdown cpus with this map actively loaded.
        for (size_t cpu = map.onprocCpus.FindSet();
            cpu < map.onprocCpus.Size();
            cpu = map.onprocCpus.FindSet(cpu + 1))
        {
            const size_t domain = AsidDomainOfCpu(asids, cpu);

            Asid asid;
            if (!AsidEntryIsLive(asids, domain, map.asids[domain], &asid))
                continue;

            if (map.asids[domain].seenTlbGen == map.tlbGen)
                continue;

            Issue(cpu, !AsidDomainIsShared(asids, domain), asid);
            map.asids[domain].seenTlbGen = map.tlbGen;
        }

        if (!AsidAnyDomainShared(asids))
            return; //fast path for targets with no shared asid domains.

        //2. Shootdown cpus that have previously loaded this map.
        for (size_t domain = 0; domain < AsidDomainCount(asids); domain++)
        {
            if (!AsidDomainIsShared(asids, domain))
                continue;

            Asid asid;
            if (!AsidEntryIsLive(asids, domain, map.asids[domain], &asid))
                continue;
            if (map.asids[domain].seenTlbGen == map.tlbGen)
                continue;

            const auto& domCpus = AsidDomainCpus(asids, domain);
            Issue(domCpus.FindSet(), false, asid);

            map.asids[domain].seenTlbGen = map.tlbGen;
        }
    }

    HwMap* HwCreateKernelMap(InitState& state, Paddr root)
    {
        //this runs before the pmap pool exists, so the object is carved from
        //the init-state allocator instead of PoolAllocWired(). It also runs
        //before the switch to the kernel page tables, so its reserved vaddr
        //isn't live yet: we map it into the kernel root for later use but
        //construct the object *now* through the bootloader direct map alias of
        //its backing page. `root` is the table early mapping has been
        //populating, so we adopt it as-is.
        NPK_ASSERT(sizeof(HwMap) <= PageSize());

        char* vaddr = state.VmAlloc(sizeof(HwMap));
        const Paddr paddr = state.PmAlloc();
        HwEarlyMap(state, paddr, reinterpret_cast<uintptr_t>(vaddr),
            MmuPermission::Write, {});

        void* build = reinterpret_cast<void*>(state.dmBase + paddr);
        auto* map = new(build) HwMap {};

        map->root = root;
        map->onprocCpus.Reset(1); //TODO: re-init with real cpu count later

        const auto& conf = GetPageTableConfig();
        if (!conf.splitRoot)
        {
            char* rootPtes = reinterpret_cast<char*>(state.dmBase + root);

            for (size_t i = conf.kernelFirstIndex; i <= conf.kernelLastIndex;
                i++)
            {
                void* pte = rootPtes + i * conf.pteSize;
                if (IsPteValid(pte))
                    continue;

                WriteIntermediatePte(conf, pte, state.PmAlloc(), true);
            }
        }

        return reinterpret_cast<HwMap*>(vaddr);
    }

    NpkStatus HwMapLeafTable(Paddr* outTable, HwMap* map, uintptr_t vaddr)
    {
        if (outTable == nullptr || map == nullptr)
            return NpkStatus::InvalidArg;

        const auto& conf = GetPageTableConfig();
        sl::ScopedLock mapLock(map->lock);

        PtWalk walk;
        switch (Walk(walk, *map, vaddr, true))
        {
        case WalkResult::Success:
            break;

        case WalkResult::BlockMapped:
            return NpkStatus::AlreadyMapped;

        case WalkResult::NoTable:
            [[fallthrough]];
        case WalkResult::NoAccess:
            return NpkStatus::Shortage;
        }

        PtMetadata(walk.tables[0]).validCount = conf.levelMask[0] + 1;
        *outTable = walk.tables[0];

        return NpkStatus::Success;
    }

    NpkStatus HwMapCreate(HwMap** outMap)
    {
        const auto& conf = GetPageTableConfig();

        void* ptr = PoolAllocWired(sizeof(HwMap), HwMapTag);
        if (ptr == nullptr)
            return NpkStatus::Shortage;
        auto* map = new(ptr) HwMap {};

        if (!map->onprocCpus.Reset(MySystemDomain().smpControls.Size()))
        {
            PoolFreeWired(ptr, sizeof(HwMap), HwMapTag);

            return NpkStatus::Shortage;
        }

        if (!AllocAsidEntries(*map))
        {
            map->onprocCpus.Destroy();
            PoolFreeWired(ptr, sizeof(HwMap), HwMapTag);

            return NpkStatus::Shortage;
        }

        auto rootPage = AllocPage(true);
        if (rootPage == nullptr)
        {
            FreeAsidEntries(*map);
            map->onprocCpus.Destroy();
            PoolFreeWired(ptr, sizeof(HwMap), HwMapTag);

            return NpkStatus::Shortage;
        }

        map->root = LookupPagePaddr(rootPage);
        PtMetadata(map->root).validCount = 0;

        if (!conf.splitRoot)
        {
            bool cloned = false;
            {
                sl::ScopedLock scopeLock(map->lock);

                auto srcRef = AccessPage(HwKernelMap()->root);
                auto destRef = AccessPage(map->root);

                if (srcRef.Valid() && destRef.Valid())
                {
                    const size_t len = (conf.kernelLastIndex
                        - conf.kernelFirstIndex + 1) * conf.pteSize;

                    auto src = PteAt(conf, srcRef, conf.kernelFirstIndex);
                    auto dest = PteAt(conf, destRef, conf.kernelFirstIndex);

                    sl::MemCopy(dest, src, len);
                    cloned = true;
                }
            }

            if (!cloned)
            {
                FreeAsidEntries(*map);
                map->onprocCpus.Destroy();
                PoolFreeWired(ptr, sizeof(HwMap), HwMapTag);
                FreePage(rootPage);

                return NpkStatus::Shortage;
            }
        }

        *outMap = map;

        return NpkStatus::Success;
    }

    void HwMapDestroy(HwMap* map)
    {
        NPK_ASSERT(map != nullptr);
        NPK_ASSERT(map != HwKernelMap());
        NPK_ASSERT(map->onprocCpus.Count() == 0);

        MapBatch batch {};
        ResetMapBatch(batch, map, {});

        map->lock.Lock();
        CollectTables(batch, map->root, GetPageTableConfig().levelCount - 1);
        batch.coarse = true; //safe to assume it'll be coarse in this case.
        map->lock.Unlock();

        HwMapUpdate(batch, true);
        FinishMapBatch(batch);

        FreeAsidEntries(*map);
        map->onprocCpus.Destroy();
        PoolFreeWired(map, sizeof(HwMap), HwMapTag);
    }

    HwMap* HwKernelMap()
    {
        return MySystemDomain().kernelMap;
    }

    void HwMapActivate(HwMap* map)
    {
        NPK_ASSERT(map != nullptr);
        NPK_ASSERT(map != HwKernelMap());

        const auto prevIpl = RaiseIpl(Ipl::Dpc);
        const size_t self = MyRelativeCoreId();
        auto* prev = *activeHwMap;

        if (prev != map)
        {
            map->lock.Lock();
            map->activeCpus.Set(self);
            map->lock.Unlock();
        }

        activeHwMap = map;
        SetUserRoot(map->root, map->asid);

        if (prev != map && prev != nullptr)
        {
            prev->lock.Lock();
            prev->onprocCpus.Clear(self);
            prev->lock.Unlock();
        }

        RestoreIpl(prevIpl);
    }

    NpkStatus HwMapAdd(MapBatch& batch, uintptr_t vaddr, Paddr paddr,
        MmuPermissions perms, MmuCacheMode cacheMode, bool wired)
    {
        (void)wired;

        if (batch.map == nullptr)
            return NpkStatus::InvalidArg;

        HwMap* map = batch.map;
        const auto& conf = GetPageTableConfig();
        const bool isKernel = (map == HwKernelMap());

        sl::ScopedLock mapLock(map->lock);

        PtWalk walk;
        switch (Walk(walk, *map, vaddr, true))
        {
        case WalkResult::Success:
            break;

        //a block mapping already covers this vaddr, and we cannot split it.
        case WalkResult::BlockMapped:
            return NpkStatus::AlreadyMapped;

        case WalkResult::NoTable:
        case WalkResult::NoAccess:
            return NpkStatus::Shortage;
        }

        auto ref = AccessPage(walk.tables[0]);
        if (!ref.Valid())
            return NpkStatus::Shortage;

        void* pte = PteAt(conf, ref, walk.indices[0]);

        PteOnStack buffer;
        MakeLeafPte(buffer.data, paddr, perms, cacheMode, isKernel, 0);

        //NOTE: the api requires that only invalid maps can be made valid,
        //if there's already a valid map here, abort!
        //NOTE2: the exception (because of course one must exist!) is that if
        //the existing paddr + flags match, we pretend the map succeeded.
        //The comparison is against the pte we just built rather than the
        //requested arguments: the encoding is lossy (no-execute on a cpu
        //without NX, write-combining without PAT), and an identical remap must
        //still be recognised as identical after passing through it.
        if (IsPteValid(pte))
        {
            if (GetPteAddr(pte) == GetPteAddr(buffer.data)
                && GetPtePerms(pte) == GetPtePerms(buffer.data)
                && GetPteCacheMode(pte) == GetPteCacheMode(buffer.data))
                return NpkStatus::Success;

            return NpkStatus::AlreadyMapped;
        }

        DoWritePte(conf, pte, buffer.data);
        PtMetadata(walk.tables[0]).validCount++;

        if (conf.cachesInvalid)
            MapBatchAddRange(batch, vaddr, PageSize());

        return NpkStatus::Success;
    }

    NpkStatus HwMapRemove(MapBatch& batch, uintptr_t vaddr, size_t count)
    {
        if (batch.map == nullptr)
            return NpkStatus::InvalidArg;

        HwMap* map = batch.map;
        const auto& conf = GetPageTableConfig();
        const uintptr_t end = vaddr + (count << PfnShift());

        sl::ScopedLock mapLock(map->lock);

        PteOnStack invalid;
        MakeInvalidPte(invalid.data);

        while (vaddr < end)
        {
            PtWalk walk;

            //a block mapping is skipped like an absent one: this path only
            //ever created leaf-sized entries, so it has no business tearing
            //down a static mapping it doesn't know the shape of.
            const auto result = Walk(walk, *map, vaddr, false);
            if (result == WalkResult::NoAccess)
                return NpkStatus::Shortage;

            if (result != WalkResult::Success)
            {
                const uintptr_t next = NextSubtree(vaddr, walk.stopLevel);
                if (next <= vaddr)
                    break;
                vaddr = next;

                continue;
            }

            const Paddr table = walk.tables[0];
            size_t index = walk.indices[0];

            auto ref = AccessPage(table);
            if (!ref.Valid())
                return NpkStatus::Shortage;

            while (index <= conf.levelMask[0] && vaddr < end)
            {
                void* pte = PteAt(conf, ref, index);
                if (!IsPteValid(pte))
                {
                    index++;
                    vaddr += PageSize();

                    continue;
                }

                PteOnStack old;
                DoExchangePte(conf, pte, invalid.data, old.data);

                if (IsPteDirty(old.data))
                    PublishDirtyBit(GetPteAddr(old.data));

                MapBatchAddRange(batch, vaddr, PageSize());
                PtMetadata(table).validCount--;

                index++;
                vaddr += PageSize();
            }

            ref = {};

            if (PtMetadata(table).validCount == 0)
                FreeEmptyTables(batch, walk, vaddr - PageSize());
        }

        return NpkStatus::Success;
    }

    NpkStatus HwMapProtect(MapBatch& batch, uintptr_t vaddr, size_t count,
        MmuPermissions perms)
    {
        if (batch.map == nullptr)
            return NpkStatus::InvalidArg;

        HwMap* map = batch.map;
        const auto& conf = GetPageTableConfig();
        const uintptr_t end = vaddr + (count << PfnShift());

        sl::ScopedLock mapLock(map->lock);

        while (vaddr < end)
        {
            PtWalk walk;

            //as in HwMapRemove(): a block mapping is left alone.
            const auto result = Walk(walk, *map, vaddr, false);
            if (result == WalkResult::NoAccess)
                return NpkStatus::NotAvailable;

            if (result != WalkResult::Success)
            {
                const uintptr_t next = NextSubtree(vaddr, walk.stopLevel);
                if (next <= vaddr)
                    break;
                vaddr = next;

                continue;
            }

            size_t index = walk.indices[0];
            auto ref = AccessPage(walk.tables[0]);
            if (!ref.Valid())
                return NpkStatus::Shortage;

            while (index <= conf.levelMask[0] && vaddr < end)
            {
                void* pte = PteAt(conf, ref, index);
                if (!IsPteValid(pte))
                {
                    index++;
                    vaddr += PageSize();

                    continue;
                }

                PteOnStack old;
                PteOnStack next;
                sl::MemCopy(old.data, pte, conf.pteSize);

                do
                {
                    sl::MemCopy(next.data, old.data, conf.pteSize);
                    SetPtePerms(next.data, perms);
                }
                while (!DoCompExchangePte(conf, pte, old.data, next.data));

                if (!perms.Has(MmuPermission::Write) && IsPteDirty(old.data))
                    PublishDirtyBit(GetPteAddr(old.data));

                MapBatchAddRange(batch, vaddr, PageSize());

                index++;
                vaddr += PageSize();
            }
        }

        return NpkStatus::Success;
    }

    HwHarvestResult HwMapUpgrade(MapBatch& batch, uintptr_t vaddr,
        Paddr frame, MmuPermissions perms)
    {
        HwHarvestResult found {};

        if (batch.map == nullptr)
            return found;

        HwMap* map = batch.map;
        const auto& conf = GetPageTableConfig();

        sl::ScopedLock mapLock(map->lock);

        PtWalk walk;
        if (Walk(walk, *map, vaddr, false) != WalkResult::Success)
            return found;

        auto ref = AccessPage(walk.tables[0]);
        if (!ref.Valid())
            return found;

        void* pte = PteAt(conf, ref, walk.indices[0]);

        PteOnStack old;
        PteOnStack next;
        sl::MemCopy(old.data, pte, conf.pteSize);
        do
        {
            if (!IsPteValid(old.data) || !IsLeafPte(old.data, 0)
                || GetPteAddr(old.data) != frame)
            {
                return found;
            }

            sl::MemCopy(next.data, old.data, conf.pteSize);
            SetPtePerms(next.data, perms);

            const auto had = GetPtePerms(old.data);
            if ((had | GetPtePerms(next.data)) != GetPtePerms(next.data))
                return found;
        }
        while (!DoCompExchangePte(conf, pte, old.data, next.data));

        found.matched = true;
        if (IsPteAccessed(old.data))
            found.state.Set(MmuPageState::Accessed);
        if (IsPteDirty(old.data))
            found.state.Set(MmuPageState::Dirty);

        if (conf.upgradeNeedsInvalidate)
            MapBatchAddRange(batch, vaddr, PageSize());

        return found;
    }

    NpkStatus HwMapExtract(Paddr* outPaddr, MmuPermissions* outPerms,
        MmuCacheMode* outMode, HwMap* map, uintptr_t vaddr)
    {
        if (map == nullptr)
            return NpkStatus::InvalidArg;

        const auto& conf = GetPageTableConfig();

        sl::ScopedLock scopeLock(map->lock);

        PtWalk walk;
        if (Walk(walk, *map, vaddr, false) != WalkResult::Success)
            return NpkStatus::BadVaddr;

        auto ref = AccessPage(walk.tables[0]);
        if (!ref.Valid())
            return NpkStatus::Shortage;

        void* pte = PteAt(conf, ref, walk.indices[0]);
        if (!IsPteValid(pte) || !IsLeafPte(pte, 0))
            return NpkStatus::BadVaddr;

        if (outPaddr != nullptr)
            *outPaddr = GetPteAddr(pte);
        if (outPerms != nullptr)
            *outPerms = GetPtePerms(pte);
        if (outMode != nullptr)
            *outMode = GetPteCacheMode(pte);

        return NpkStatus::Success;
    }

    HwHarvestResult HwMapHarvest(MapBatch& batch, uintptr_t vaddr,
        Paddr frame, HwHarvestAction action, MmuPageStates clear)
    {
        HwHarvestResult found {};

        if (batch.map == nullptr)
            return found;

        HwMap* map = batch.map;
        const auto& conf = GetPageTableConfig();

        sl::ScopedLock mapLock(map->lock);

        PtWalk walk;
        if (Walk(walk, *map, vaddr, false) != WalkResult::Success)
            return found;

        const Paddr table = walk.tables[0];
        auto ref = AccessPage(table);
        if (!ref.Valid())
            return found;

        void* pte = PteAt(conf, ref, walk.indices[0]);

        PteOnStack invalid;
        MakeInvalidPte(invalid.data);

        PteOnStack old;
        sl::MemCopy(old.data, pte, conf.pteSize);

        while (true)
        {
            if (!IsPteValid(old.data) || GetPteAddr(old.data) != frame)
                return found;

            bool keep = action == HwHarvestAction::Keep;
            if (action == HwHarvestAction::RemoveIfIdle)
                keep = IsPteAccessed(old.data);

            if (keep)
            {
                found.matched = true;
                found.state = HarvestEntry(batch, pte, vaddr, clear);

                return found;
            }

            if (DoCompExchangePte(conf, pte, old.data, invalid.data))
                break;
        }

        found.matched = true;
        found.removed = true;
        if (IsPteAccessed(old.data))
            found.state.Set(MmuPageState::Accessed);
        if (IsPteDirty(old.data))
            found.state.Set(MmuPageState::Dirty);

        MapBatchAddRange(batch, vaddr, PageSize());
        PtMetadata(table).validCount--;

        ref = {};

        if (PtMetadata(table).validCount == 0)
            FreeEmptyTables(batch, walk, vaddr);

        return found;
    }

    size_t HwMapHarvestRange(MapBatch& batch, uintptr_t vaddr, size_t count,
        MmuPageStates clear, sl::Span<MmuRangeHarvest> out)
    {
        NPK_ASSERT(!clear.Has(MmuPageState::Dirty));

        if (batch.map == nullptr)
            return 0;

        HwMap* map = batch.map;
        const auto& conf = GetPageTableConfig();
        const size_t limit = sl::Min(count, out.Size());

        sl::ScopedLock scopeLock(map->lock);

        PtWalk walk;
        if (Walk(walk, *map, vaddr, false) != WalkResult::Success)
            return 0;

        auto ref = AccessPage(walk.tables[0]);
        if (!ref.Valid())
            return 0;

        size_t found = 0;
        size_t index = walk.indices[0];
        for (size_t i = 0; i < limit && index <= conf.levelMask[0]; i++)
        {
            void* pte = PteAt(conf, ref, index);
            const uintptr_t at = vaddr + (i << PfnShift());
            index++;

            if (!IsPteValid(pte) || !IsLeafPte(pte, 0))
                continue;

            out[found].vaddr = at;
            out[found].frame = GetPteAddr(pte);
            out[found].state = HarvestEntry(batch, pte, at, clear);
            found++;
        }

        return found;
    }

    MmuAgingCost HwGetMmuAgingCost()
    {
        if (!GetPageTableConfig().hwAccessedBit)
            return MmuAgingCost::FaultDriven;
        if (HwHasBroadcastInvalidate())
            return MmuAgingCost::CheapShootdown;

        return MmuAgingCost::ExpensiveShootdown;
    }

    bool HwHandleMinorFaultOnMap(HwMap* map, uintptr_t vaddr, bool write)
    {
        if (map == nullptr)
            return false;

        const auto& conf = GetPageTableConfig();

        if (conf.hwAccessedBit && conf.hwDirtyBit)
            return false;

        //NOTE: we can't take the map's lock here since we're above DPC IPL,
        //so instead the minor fault code is written using atomic primitives.
        //The only issue this doesnt solve is page tables being freed while 
        //we're walking them. For that the solution is to raise the local IPL
        //to Tlb level: while at this level this cpu wont acknowledge any tlb
        //invalidations, since these are processed when lowering from that IPL.
        //Therefore being at this level prevents any page tables this cpu knows
        //about (due to them being part of the current map) being freed while
        //we walk them.
        const auto prevIpl = EnsureIpl(Ipl::Tlb);
        const bool handled = DoMinorFault(*map, vaddr, write);
        RestoreIpl(prevIpl);

        return handled;
    }

    void HwMapUpdate(MapBatch& batch, bool sync)
    {
        if (batch.map == nullptr)
            return;

        if (batch.count == 0 && !batch.coarse && batch.freed.Empty())
        {
            if (sync)
                TlbSyncWait();

            return;
        }

        HwMap* map = batch.map;
        map->lock.Lock();

        PageList dead {};
        const auto exchanged = dead.Exchange(batch.freed);
        NPK_ASSERT(exchanged);

        if (map == MySystemDomain().kernelMap)
            CoverKernelMap(batch);
        else
            CoverUserMap(*map, batch);

        MapBatchDropRanges(batch);

        TlbSyncReclaim(dead);
        map->lock.Unlock();

        if (sync)
            TlbSyncWait();
    }
}
