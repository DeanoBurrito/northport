#include <private/Core.hpp>
#include <private/Vm.hpp>
#include <lib/Units.hpp>

//TODO: check max stack usage after freeing for more fun stats
//- check for non-zero values should get us pretty close.
namespace Npk
{
    constexpr HeapTag KernelStackTag = NPK_MAKE_HEAP_TAG("Stck");
    constexpr size_t DefaultStackCacheLimit = 8;

    struct CachedStack
    {
        sl::ListHook hook;
    };

    using CachedStackList = sl::List<CachedStack, &CachedStack::hook>;

    struct StackCacheCounters
    {
        sl::Atomic<size_t> reused;
        sl::Atomic<size_t> stolen;
        sl::Atomic<size_t> released;
    };

    struct StackShard
    {
        IplSpinLock<Ipl::Dpc> lock;
        CachedStackList list;
        sl::Atomic<size_t> count; //written under `lock`, can be read without
    };

    struct KernelStackCache
    {
        alignas(HwGetStaticCacheLineSize()) 
        StackShard ready;
        StackCacheCounters counters;

        //doomed stacks can be stolen by other cpus.
        alignas(HwGetStaticCacheLineSize())
        StackShard doomed;
    };

    static size_t stackCacheLimit; //TODO: refine this at runtime based on use
    static sl::Atomic<size_t> liveStacks;
    static sl::Atomic<size_t> peakStacks;
    static sl::Atomic<size_t> stacksAllocated;
    static sl::Span<KernelStackCache> stackCaches;

    static void* StackBase(void* top)
    {
        auto addr = reinterpret_cast<uintptr_t>(top);
        addr -= KernelStackSize();

        return reinterpret_cast<void*>(addr);
    }

    static void* StackTop(void* base)
    {
        auto addr = reinterpret_cast<uintptr_t>(base);
        addr += KernelStackSize();

        return reinterpret_cast<void*>(addr);
    }

    static CachedStack* NodeOf(void* top)
    {
        return static_cast<CachedStack*>(top) - 1;
    }

    static void* TopOf(CachedStack* node)
    {
        return node + 1;
    }

    //NOTE: counters written by owning cpu at DPC IPL.
    static void IncrementCounter(sl::Atomic<size_t>& counter)
    {
        counter.Store(counter.Load(sl::Relaxed) + 1, sl::Relaxed);
    }

    static void AddCount(StackShard& shard, size_t count)
    {
        shard.count.Store(shard.count.Load(sl::Relaxed) + count, sl::Relaxed);
    }

    static void SubCount(StackShard& shard, size_t count)
    {
        shard.count.Store(shard.count.Load(sl::Relaxed) - count, sl::Relaxed);
    }

    static CachedStack* TryStealStack(size_t self)
    {
        const auto count = stackCaches.Size();

        for (size_t i = 1; i < count; i++)
        {
            auto& doomed = stackCaches[(self + i) % count].doomed;

            if (doomed.count.Load(sl::Relaxed) == 0)
                continue;

            if (!doomed.lock.TryLock())
                continue;

            CachedStack* node = nullptr;
            if (!doomed.list.Empty())
            {
                SubCount(doomed, 1);
                node = doomed.list.PopFront();
            }
            doomed.lock.Unlock();

            if (node != nullptr)
                return node;
        }

        return nullptr;
    }

    static CachedStack* PopShard(StackShard& shard)
    {
        sl::ScopedLock scopeLock(shard.lock);

        if (shard.list.Empty())
            return nullptr;

        SubCount(shard, 1);

        return shard.list.PopFront();
    }

    static void* TakeCachedStack()
    {
        const auto self = MyRelativeCoreId();
        auto& cache = stackCaches[self];

        auto* node = PopShard(cache.ready);
        if (node == nullptr)
            node = PopShard(cache.doomed);

        if (node == nullptr)
        {
            node = TryStealStack(self);
            if (node == nullptr)
                return nullptr;

            IncrementCounter(cache.counters.stolen);
        }

        IncrementCounter(cache.counters.reused);

        return TopOf(node);
    }

    NpkStatus Private::InitStackCaches(size_t cpuCount)
    {
        if (cpuCount == 0)
            return NpkStatus::InvalidArg;

        stackCacheLimit = ReadConfigUint("npk.vm.stack_cache_init_limit",
            DefaultStackCacheLimit);

        const size_t len = cpuCount * sizeof(KernelStackCache)
            + HwGetStaticCacheLineSize();
        auto* ptr = PoolAllocWired(len, KernelStackTag);
        if (ptr == nullptr)
            return NpkStatus::Shortage;

        auto* base = sl::AlignUp(static_cast<char*>(ptr),
            HwGetStaticCacheLineSize());
        auto* caches = reinterpret_cast<KernelStackCache*>(base);

        for (size_t i = 0; i < cpuCount; i++)
            new (&caches[i]) KernelStackCache {};
        stackCaches = { caches, cpuCount };

        auto conv = sl::ConvertUnits(stackCacheLimit << PfnShift());
        Log("Kernel stack caches: init limit of %zu each (%zu.%zu %sB each)",
            LogLevel::Info, stackCacheLimit, conv.major, conv.minor,
            conv.prefix);

        return NpkStatus::Success;
    }

    NpkStatus AllocKernelStack(void** stack)
    {
        if (stack == nullptr)
            return NpkStatus::InvalidArg;

        if (!stackCaches.Empty())
        {
            const auto prevIpl = RaiseIpl(Ipl::Dpc);
            void* cached = TakeCachedStack();
            LowerIpl(prevIpl);

            if (cached != nullptr)
            {
                sl::MemSet(StackBase(cached), 0, KernelStackSize());
                *stack = cached;

                return NpkStatus::Success;
            }
        }

        void* ptr = PoolAllocWired(KernelStackSize(), KernelStackTag);
        if (ptr == nullptr)
            return NpkStatus::Shortage;

        const auto live = liveStacks.FetchAdd(1, sl::AcqRel) + 1;
        auto peak = peakStacks.Load(sl::Relaxed);
        while (peak < live)
        {
            if (peakStacks.CompareExchange(peak, live, sl::AcqRel))
                break;
        }

        stacksAllocated.FetchAdd(1, sl::Relaxed);
        *stack = StackTop(ptr);

        return NpkStatus::Success;
    }

    static void PushReadyStack(CachedStack* node)
    {
        auto& cache = stackCaches[MyRelativeCoreId()];
        IncrementCounter(cache.counters.released);

        CachedStackList excess;
        size_t excessCount = 0;
        {
            sl::ScopedLock readyLock(cache.ready.lock);

            cache.ready.list.PushFront(node);
            AddCount(cache.ready, 1);

            while (cache.ready.count.Load(sl::Relaxed) > stackCacheLimit)
            {
                excess.PushBack(cache.ready.list.PopBack());
                SubCount(cache.ready, 1);
                excessCount++;
            }
        }

        if (excessCount == 0)
            return;

        sl::ScopedLock doomedLock(cache.doomed.lock);
        while (!excess.Empty())
            cache.doomed.list.PushBack(excess.PopFront());

        AddCount(cache.doomed, excessCount);
    }

    void FreeKernelStack(void* stack)
    {
        NPK_CHECK(stack != nullptr, );
        NPK_ASSERT(!stackCaches.Empty());

        auto* node = new(NodeOf(stack)) CachedStack {};

        const auto prevIpl = EnsureIpl(Ipl::Dpc);
        PushReadyStack(node);
        RestoreIpl(prevIpl);
    }

    static size_t FreeStackList(KernelStackCache& cache,
        CachedStackList& list, sl::TimeCount timeout)
    {
        size_t freed = 0;
        CachedStackList refused;
        size_t refusedCount = 0;

        while (!list.Empty())
        {
            auto* node = list.PopFront();
            void* base = StackBase(TopOf(node));

            const auto result = PoolFreeWired(base, KernelStackSize(),
                KernelStackTag, timeout);

            if (result != NpkStatus::Success)
            {
                refused.PushBack(new(node) CachedStack {});
                refusedCount++;

                continue;
            }

            liveStacks.FetchSub(1, sl::AcqRel);
            freed++;
        }

        if (refusedCount != 0)
        {
            sl::ScopedLock doomedLock(cache.doomed.lock);

            while (!refused.Empty())
                cache.doomed.list.PushBack(refused.PopFront());
            AddCount(cache.doomed, refusedCount);
        }

        return freed;
    }

    static size_t TrimOneStackCache(KernelStackCache& cache, size_t keep,
        size_t limit, sl::TimeCount timeout)
    {
        CachedStackList victims;
        size_t taken = 0;

        cache.doomed.lock.Lock();
        while (!cache.doomed.list.Empty() && taken < limit)
        {
            victims.PushBack(cache.doomed.list.PopFront());

            SubCount(cache.doomed, 1);
            taken++;
        }
        cache.doomed.lock.Unlock();

        //avoid taking the lock of another cpu's hot list if we dont need to.
        if (taken < limit)
        {
            auto& ready = cache.ready;

            ready.lock.Lock();
            while (ready.count.Load(sl::Relaxed) > keep && taken < limit)
            {
                victims.PushBack(ready.list.PopBack());
                SubCount(cache.ready, 1);
                taken++;
            }
            ready.lock.Unlock();
        }

        if (victims.Empty())
            return 0;

        return FreeStackList(cache, victims, timeout);
    }

    size_t Private::TrimKernelStackCaches(size_t keepPerCpu,
        sl::TimeCount timeout)
    {
        if (stackCaches.Empty())
            return 0;

        size_t freed = 0;
        for (size_t i = 0; i < stackCaches.Size(); i++)
        {
            freed += TrimOneStackCache(stackCaches[i], keepPerCpu,
                static_cast<size_t>(-1), timeout);
        }

        return freed;
    }

    void GetStackCacheStats(StackCacheStats& out)
    {
        out = {};
        out.allocated = stacksAllocated.Load(sl::Relaxed);
        out.live = liveStacks.Load(sl::Relaxed);
        out.peak = peakStacks.Load(sl::Relaxed);

        if (stackCaches.Empty())
            return;

        for (size_t i = 0; i < stackCaches.Size(); i++)
        {
            auto& cache = stackCaches[i];

            out.reused += cache.counters.reused.Load(sl::Relaxed);
            out.stolen += cache.counters.stolen.Load(sl::Relaxed);
            out.released += cache.counters.released.Load(sl::Relaxed);
            out.cached += cache.ready.count.Load(sl::Relaxed);
            out.doomed += cache.doomed.count.Load(sl::Relaxed);
        }
    }
}
