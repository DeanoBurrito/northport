#include <hardware/common/mmu/PageTables.hpp>
#include <hardware/common/mmu/TlbSync.hpp>
#include <Core.hpp>
#include <Vm.hpp>
#include <private/Entry.hpp>
#include <lib/Locks.hpp>

namespace Npk
{
    constexpr HeapTag HwMapTag = NPK_MAKE_HEAP_TAG("Hmap");
    constexpr size_t MaxPteSize = 16;
    constexpr size_t MaxPtPathLevels = 8;
    constexpr size_t MaxPendingRanges = 8;

    struct alignas(MaxPteSize) PteOnStack
    {
        uint8_t data[MaxPteSize];
    };

    //this assert is very unscientific, I've just chosen the worst case I can
    //think of (PAE on 32bit x86) and accounted for it.
    static_assert(MaxPteSize >= 2 * sizeof(uintptr_t));

    //the pmap interface only requires synchronization between cpus on calls
    //to `HwMapUpdate()`. This struct tracks a minimal amount of changed vaddrs
    //between update calls, so we know what to sync with other cpus.
    struct PendingRanges
    {
        uintptr_t bases[MaxPendingRanges];
        size_t lengths[MaxPendingRanges];
        size_t count;

        void Reset()
        {
            count = 0;
        }

        bool IsFull() const
        {
            return count == MaxPendingRanges;
        }

        void Saturate()
        {
            count = MaxPendingRanges;
        }

        void Add(uintptr_t base, size_t length)
        {
            if (IsFull())
                return;

            for (size_t i = 0; i < count; i++)
            {
                if (base == bases[i] + lengths[i])
                {
                    lengths[i] += length;

                    return;
                }
            
                if (base + length == bases[i])
                {
                    bases[i] -= length;
                    lengths[i] += length;

                    return;
                }
            }

            bases[count] = base;
            lengths[count] = length;
            count++;
        }
    };

    struct HwMap
    {
        IplSpinLock<Ipl::Dpc> lock;
        Paddr root;
        Asid asid; //TODO: support properly!

        CpuBitset activeCpus;
        PendingRanges pendingUpdates;
        PageList pendingFree;
    };

    struct PtPageInfo
    {
        sl::FwdListHook freeHook;
        uint16_t validCount;
    };
    static_assert(sizeof(PtPageInfo) <= sizeof(PageInfo));
    static_assert(offsetof(PtPageInfo, freeHook) == 0);
    static_assert(alignof(decltype(PtPageInfo::freeHook)) > 1);

    struct PtWalkPath
    {
        Paddr tables[MaxPtPathLevels];
        size_t indices[MaxPtPathLevels];
    };

    enum class WalkResult
    {
        Success,
        NoTable,
        BlockMapped,
    };

    CPU_LOCAL(HwMap*, activeHwMap);

    static void DoWritePte(const PageTableConfig& conf, size_t pteSize, 
        void* dest, const void* source)
    {
        if (conf.hasCustomWritePte)
            return WritePte(dest, source);

        switch (pteSize)
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

    static void ExchangePte(const PageTableConfig& conf, size_t pteSize,
        void* dest, const void* source, void* prev)
    {
        if (conf.hasCustomExchange)
            return ExchangePte(dest, source, prev);

        switch (pteSize)
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

    static bool CompExchangePte(const PageTableConfig& conf, size_t pteSize,
        void* dest, void* expected, const void* desired)
    {
        if (conf.hasCustomCompareExchange)
            return CompareExchangePte(dest, expected, desired);

        switch (pteSize)
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

    static void PublishDirtyBit(Paddr paddr)
    {
        if (!PaddrHasPageInfo(paddr))
            return;

        LookupPageInfo(paddr)->vm.flags.Clear(PageVmFlag::Clean);
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
                const size_t index = (vaddr >> conf.levelShift[level])
                    & conf.levelMask[level];

                auto ref = AccessPage(current);
                NPK_ASSERT(ref.Valid()); //TODO: non-fatal handling

                void* pte = static_cast<char*>(ref.vaddr) + index 
                    * conf.pteSize;

                if (IsPteValid(pte))
                    next = GetPteAddr(pte);
            }

            auto* info = LookupPageInfo(current);
            FreePage(info);

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

            Paddr pagePaddr = LookupPagePaddr(page);
            PtMetadata(pagePaddr).validCount = 1;

            if (level == 0)
            {
                PtMetadata(pagePaddr).validCount = 0;
                outLeaf = pagePaddr;
            }
            else
            {
                const size_t index = (vaddr >> conf.levelShift[level])
                    & conf.levelMask[level];
                auto ref = AccessPage(pagePaddr);

                PteOnStack buffer;
                MakeIntermediatePte(buffer.data, child, global);

                const uintptr_t dest = index * conf.pteSize 
                    + reinterpret_cast<uintptr_t>(ref.vaddr);
                DoWritePte(conf, conf.pteSize, reinterpret_cast<void*>(dest),
                    buffer.data);
            }

            child = pagePaddr;
        }

        outTop = child;

        return true;
    }

    static WalkResult Walk(Paddr& outTable, size_t& outIndex, HwMap& map,
        uintptr_t vaddr, bool alloc, PtWalkPath* path = nullptr)
    {
        const auto& conf = GetPageTableConfig();
        const bool isKernel = &map == HwKernelMap();

        if (path != nullptr)
            NPK_ASSERT(conf.levelCount <= MaxPtPathLevels);

        Paddr currentPt = map.root;
        for (size_t level = conf.levelCount - 1; level > 0; level--)
        {
            const size_t index = (vaddr >> conf.levelShift[level])
                & conf.levelMask[level];

            if (path != nullptr)
            {
                path->tables[level] = currentPt;
                path->indices[level] = index;
            }

            auto ref = AccessPage(currentPt);
            if (!ref.Valid())
                return WalkResult::NoTable;

            void* pte = static_cast<char*>(ref.vaddr) + index * conf.pteSize;

            if (IsPteValid(pte))
            {
                if (IsLeafPte(pte, level))
                    return WalkResult::BlockMapped;

                currentPt = GetPteAddr(pte);
                continue;
            }
            if (!alloc)
                return WalkResult::NoTable;

            //NOTE: we dont currently support `alloc` being used with path
            //tracking. If it's needed it'll need support in BuildChain().
            NPK_ASSERT(path == nullptr);

            Paddr top;
            Paddr leaf;
            if (!BuildChain(top, leaf, vaddr, level, isKernel))
                return WalkResult::NoTable;

            PteOnStack buffer;
            MakeIntermediatePte(buffer.data, top, isKernel);
            DoWritePte(conf, conf.pteSize, pte, buffer.data);

            PtMetadata(currentPt).validCount++;

            outTable = leaf;
            outIndex = (vaddr >> conf.levelShift[0]) & conf.levelMask[0];

            return WalkResult::Success;
        }

        outTable = currentPt;
        outIndex = (vaddr >> conf.levelShift[0]) & conf.levelMask[0];

        if (path != nullptr)
        {
            path->tables[0] = currentPt;
            path->indices[0] = outIndex;
        }

        return WalkResult::Success;
    }

    static uintptr_t NextSubtree(uintptr_t vaddr, size_t level)
    {
        const auto& conf = GetPageTableConfig();
        const uintptr_t span = static_cast<uintptr_t>(1)
            << conf.levelShift[level];

        auto next = vaddr + span;
        next &= span - 1;

        return next;
    }

    static void FreeEmptyTables(HwMap& map, const PtWalkPath& path,
        uintptr_t vaddr)
    {
        const auto& conf = GetPageTableConfig();
        const bool isKernel = &map == HwKernelMap();

        for (size_t level = 0; level + 1 < conf.levelCount; level++)
        {
            const Paddr table = path.tables[level];

            if (PtMetadata(table).validCount != 0)
                break;

            const Paddr parent = path.tables[level + 1];
            if (isKernel && parent == map.root)
                break;

            auto ref = AccessPage(parent);
            NPK_ASSERT(ref.Valid());

            void* pte = static_cast<char*>(ref.vaddr) + path.indices[level + 1]
                * conf.pteSize;

            PteOnStack invalid;
            MakeInvalidPte(invalid.data);
            DoWritePte(conf, conf.pteSize, pte, invalid.data);

            PtMetadata(parent).validCount--;

            const uintptr_t span = (conf.levelMask[level] + 1)
                << conf.levelShift[level];
            map.pendingUpdates.Add(vaddr & ~(span - 1), span);

            map.pendingFree.PushBack(LookupPageInfo(table));
        }
    }

    NpkStatus HwMapCreate(HwMap** outMap)
    {
        const auto& conf = GetPageTableConfig();

        void* ptr = PoolAllocWired(sizeof(HwMap), HwMapTag);
        if (ptr == nullptr)
            return NpkStatus::Shortage;
        auto* map = new(ptr) HwMap {};

        if (!map->activeCpus.Reset(MySystemDomain().smpControls.Size()))
        {
            PoolFreeWired(ptr, sizeof(HwMap), HwMapTag);

            return NpkStatus::Shortage;
        }

        auto rootPage = AllocPage(true);
        if (rootPage == nullptr)
        {
            map->activeCpus.Destroy();
            PoolFreeWired(ptr, sizeof(HwMap), HwMapTag);

            return NpkStatus::Shortage;
        }

        map->root = LookupPagePaddr(rootPage);
        PtMetadata(map->root).validCount = 0;

        map->pendingUpdates.Reset();

        if (!conf.splitRoot)
        {
            //clone the static kernel slots into the new table since there is
            //only a single root.

            auto srcRef = AccessPage(HwKernelMap()->root);
            auto destRef = AccessPage(map->root);
            if (!srcRef.Valid() || !destRef.Valid())
            {
                map->activeCpus.Destroy();
                PoolFreeWired(ptr, sizeof(HwMap), HwMapTag);
                FreePage(rootPage);

                return NpkStatus::Shortage;
            }

            const size_t len = (conf.kernelLastIndex - conf.kernelFirstIndex +1)
                * conf.pteSize;
            auto* src = static_cast<const char*>(srcRef.vaddr) 
                + conf.kernelFirstIndex * conf.pteSize;
            auto* dest = static_cast<char*>(destRef.vaddr)
                + conf.kernelFirstIndex * conf.pteSize;

            sl::MemCopy(dest, src, len);
        }

        *outMap = map;

        return NpkStatus::Success;
    }

    static void CollectTables(HwMap& map, Paddr table, size_t level)
    {
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

                void* pte = static_cast<char*>(ref.vaddr) + i * conf.pteSize;

                if (!IsPteValid(pte))
                    continue;
                if (IsLeafPte(pte, level))
                    continue;

                CollectTables(map, GetPteAddr(pte), level - 1);
            }
        }

        map.pendingFree.PushBack(LookupPageInfo(table));
    }

    void HwMapDestroy(HwMap* map)
    {
        NPK_ASSERT(map != nullptr);
        NPK_ASSERT(map != HwKernelMap());
        NPK_ASSERT(map->activeCpus.Count() == 0);

        map->lock.Lock();
        CollectTables(*map, map->root, GetPageTableConfig().levelCount - 1);
        map->pendingUpdates.Saturate();
        map->lock.Unlock();

        PageList emptyList {};
        HwMapUpdate(map, true, emptyList);

        map->activeCpus.Destroy();
        PoolFreeWired(map, sizeof(HwMap), HwMapTag);
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
        map->activeCpus.Reset(1); //TODO: re-init with real cpu count later
        map->pendingUpdates.Reset();

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

                PteOnStack buffer;
                MakeIntermediatePte(buffer.data, state.PmAlloc(), true);
                DoWritePte(conf, conf.pteSize, pte, buffer.data);
            }
        }

        return reinterpret_cast<HwMap*>(vaddr);
    }

    HwMap* HwKernelMap()
    {
        return MySystemDomain().kernelMap;
    }

    void HwMapActivate(HwMap* map)
    {
        NPK_ASSERT(map != nullptr);

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
            prev->activeCpus.Clear(self);
            prev->lock.Unlock();
        }
    }

    NpkStatus HwMapAdd(HwMap* map, uintptr_t vaddr, Paddr paddr, 
        MmuPermissions perms, MmuCacheMode cacheMode, bool wired)
    {
        (void)wired;

        if (map == nullptr)
            return NpkStatus::InvalidArg;

        const auto& conf = GetPageTableConfig();
        const bool isKernel = (map == HwKernelMap());

        sl::ScopedLock mapLock(map->lock);

        Paddr table;
        size_t index;
        switch (Walk(table, index, *map, vaddr, true))
        {
        case WalkResult::Success:
            break;

        //a block mapping already covers this vaddr, and we cannot split it.
        case WalkResult::BlockMapped:
            return NpkStatus::AlreadyMapped;

        case WalkResult::NoTable:
            return NpkStatus::Shortage;
        }

        auto ref = AccessPage(table);
        if (!ref.Valid())
            return NpkStatus::Shortage;

        void* pte = static_cast<char*>(ref.vaddr) + index * conf.pteSize;

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

        DoWritePte(conf, conf.pteSize, pte, buffer.data);
        PtMetadata(table).validCount++;

        return NpkStatus::Success;
    }

    NpkStatus HwMapRemove(HwMap* map, uintptr_t vaddr, size_t count)
    {
        if (map == nullptr)
            return NpkStatus::InvalidArg;

        const auto& conf = GetPageTableConfig();
        const uintptr_t end = vaddr + (count << PfnShift());

        sl::ScopedLock mapLock(map->lock);

        while (vaddr < end)
        {
            Paddr table;
            size_t index;
            PtWalkPath path;
            //a block mapping is skipped like an absent one: this path only
            //ever created leaf-sized entries, so it has no business tearing
            //down a static mapping it doesn't know the shape of.
            if (Walk(table, index, *map, vaddr, false, &path)
                != WalkResult::Success)
            {
                const uintptr_t next = NextSubtree(vaddr, level);
                if (next <= vaddr)
                    break;
                vaddr = next;

                continue;
            }

            auto ref = AccessPage(table);
            if (!ref.Valid())
                return NpkStatus::Shortage;

            while (index <= conf.levelMask[0] && vaddr < end)
            {
                PteOnStack invalid;
                PteOnStack old;

                void* pte = static_cast<char*>(ref.vaddr) + index
                    * conf.pteSize;
                if (!IsPteValid(pte))
                {
                    index++;
                    vaddr += PageSize();

                    continue;
                }

                MakeInvalidPte(invalid.data);
                ExchangePte(conf, conf.pteSize, pte, invalid.data, old.data);

                if (IsPteDirty(old.data))
                    PublishDirtyBit(GetPteAddr(old.data));

                map->pendingUpdates.Add(vaddr, PageSize());
                PtMetadata(table).validCount--;

                index++;
                vaddr += PageSize();
            }

            ref = {};

            if (PtMetadata(table).validCount == 0)
                FreeEmptyTables(*map, path, vaddr - PageSize());
        }

        return NpkStatus::Success;
    }

    NpkStatus HwMapProtect(HwMap* map, uintptr_t vaddr, size_t count, 
        MmuPermissions perms)
    {
        if (map == nullptr)
            return NpkStatus::InvalidArg;

        const auto& conf = GetPageTableConfig();
        const uintptr_t end = vaddr + (count << PfnShift());

        sl::ScopedLock mapLock(map->lock);

        while (vaddr < end)
        {
            Paddr table;
            size_t index;
            //as in HwMapRemove(): a block mapping is left alone.
            if (Walk(table, index, *map, vaddr, false) != WalkResult::Success)
            {
                const uintptr_t next = NextSubtree(vaddr, level);
                if (next <= vaddr)
                    break;
                vaddr = next;

                continue;
            }

            auto ref = AccessPage(table);
            if (!ref.Valid())
                return NpkStatus::Shortage;

            while (index <= conf.levelMask[0] && vaddr < end)
            {
                void* pte = static_cast<char*>(ref.vaddr) + index
                    * conf.pteSize;
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
                while (!CompExchangePte(conf, conf.pteSize, pte, old.data,
                    next.data));

                if (!perms.Has(MmuPermission::Write) && IsPteDirty(old.data))
                    PublishDirtyBit(GetPteAddr(old.data));

                map->pendingUpdates.Add(vaddr, PageSize());

                index++;
                vaddr += PageSize();
            }
        }

        return NpkStatus::Success;
    }

    NpkStatus HwMapExtract(Paddr* outPaddr, MmuPermissions* outPerms, 
        MmuCacheMode* outMode, HwMap* map, uintptr_t vaddr)
    {
        if (map == nullptr)
            return NpkStatus::InvalidArg;

        const auto& conf = GetPageTableConfig();

        sl::ScopedLock scopeLock(map->lock);

        Paddr table;
        size_t index;
        if (Walk(table, index, *map, vaddr, false) != WalkResult::Success)
            return NpkStatus::BadVaddr;

        auto ref = AccessPage(table);
        if (!ref.Valid())
            return NpkStatus::Shortage;

        void* pte = static_cast<char*>(ref.vaddr) + index * conf.pteSize;
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

    void HwMapSetWired(HwMap* map, uintptr_t vaddr, bool wired)
    {
        (void)map;
        (void)vaddr;
        (void)wired;

        //this function is a no-op as page tables aren't lossy, so all entries
        //are wired from the PoV of the pmap.
    }

    static bool GetOrClearAdBits(HwMap* map, uintptr_t vaddr, bool dirtyBit,
        bool clear)
    {
        if (map == nullptr)
            return false;

        const auto& conf = GetPageTableConfig();

        sl::ScopedLock scopeLock(map->lock);

        Paddr table;
        size_t index;
        if (Walk(table, index, *map, vaddr, false) != WalkResult::Success)
            return false;

        auto ref = AccessPage(table);
        if (!ref.Valid())
            return false;

        void* pte = static_cast<char*>(ref.vaddr) + index * conf.pteSize;
        if (!IsPteValid(pte))
            return false;

        bool prevValue = false;

        if (clear)
        {
            if (dirtyBit)
                prevValue = ClearPteDirty(pte);
            else
                prevValue = ClearPteAccessed(pte);
        }
        else
        {
            if (dirtyBit)
                prevValue = IsPteDirty(pte);
            else
                prevValue = IsPteAccessed(pte);
        }

        if (clear && prevValue)
            map->pendingUpdates.Add(vaddr, PageSize());

        return prevValue;
    }

    bool HwMapGetAccessed(HwMap* map, uintptr_t vaddr)
    {
        return GetOrClearAdBits(map, vaddr, false, false);
    }

    bool HwMapGetDirty(HwMap* map, uintptr_t vaddr)
    {
        return GetOrClearAdBits(map, vaddr, true, false);
    }

    bool HwMapClearAccessed(HwMap* map, uintptr_t vaddr)
    {
        return GetOrClearAdBits(map, vaddr, false, true);
    }

    bool HwMapClearDirty(HwMap* map, uintptr_t vaddr)
    {
        return GetOrClearAdBits(map, vaddr, true, true);
    }

    static bool DoMinorFault(HwMap& map, uintptr_t vaddr, bool write)
    {
        const auto& conf = GetPageTableConfig();

        Paddr table;
        size_t index;
        if (Walk(table, index, map, vaddr, false) != WalkResult::Success)
            return false;

        auto ref = AccessPage(table);
        if (!ref.Valid())
            return false;

        void* pte = static_cast<char*>(ref.vaddr) + index * conf.pteSize;
        if (!IsPteValid(pte))
            return false;

        if (write)
        {
            if (conf.hwDirtyBit)
                return false;

            if (!PteIsWriteTrackable(pte))
                return false;

            SetPteDirty(pte);
            PublishDirtyBit(GetPteAddr(pte));
            SetPteAccessed(pte);

            return true;
        }

        return !SetPteAccessed(pte);
    }

    bool HwHandleMinorFaultOnMap(HwMap* map, uintptr_t vaddr, bool write)
    {
        if (map == nullptr)
            return false;

        const auto& conf = GetPageTableConfig();

        if (conf.hwAccessedBit)
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
        const Ipl prevIpl = CurrentIpl();
        const bool raised = prevIpl < Ipl::Tlb;
        if (raised)
            RaiseIpl(Ipl::Tlb);

        const bool handled = DoMinorFault(*map, vaddr, write);

        if (raised)
            LowerIpl(prevIpl);

        return handled;
    }

    void HwMapUpdate(HwMap* map, bool sync, PageList& freeAfter)
    {
        NPK_ASSERT(map != nullptr);

        map->lock.Lock();

        const PendingRanges ranges = map->pendingUpdates;
        map->pendingUpdates.Reset();

        PageList dead {};
        while (!map->pendingFree.Empty())
            dead.PushBack(map->pendingFree.PopFront());

        while (!freeAfter.Empty())
            dead.PushBack(freeAfter.PopFront());

        //the kernel map shouldn't have a target list of cpus since its active
        //on all of them.
        const CpuBitset* targets = (map == MySystemDomain().kernelMap) 
            ? nullptr : &map->activeCpus;

        if (HwHasBroadcastInvalidate())
        {
            if (ranges.IsFull())
                HwInvalidateTlbs(map, 0, static_cast<size_t>(-1));
            else
            {
                for (size_t i = 0; i < ranges.count; i++)
                    HwInvalidateTlbs(map, ranges.bases[i], ranges.lengths[i]);
            }

            HwSyncTlbs();
            map->lock.Unlock();

            FreePageList(dead);

            return;
        }
        //else: no hardware support, software tlb sync path.

        if (ranges.IsFull())
            TlbSyncDepositAll(targets, map->asid);
        else
        {
            for (size_t i = 0; i < ranges.count; i++)
            {
                TlbSyncDeposit(targets, map->asid, ranges.bases[i],
                    ranges.lengths[i]);
            }
        }

        TlbSyncReclaim(dead);
        map->lock.Unlock();

        if (sync)
            TlbSyncWait();
    }
}
