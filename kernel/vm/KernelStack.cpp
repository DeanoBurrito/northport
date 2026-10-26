#include <private/Core.hpp>
#include <private/Vm.hpp>
#include <lib/Units.hpp>
#include <lib/Maths.hpp>

//TODO: check max stack usage after freeing for more fun stats
//- check for non-zero values should get us pretty close.
namespace Npk
{
    constexpr HeapTag KernelStackTag = NPK_MAKE_HEAP_TAG("Stck");
    constexpr size_t LimitFracBits = 4;
    constexpr size_t LimitEwmaShift = 2;
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
        size_t lowCountSample;
        size_t highCountSample;
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

    static sl::Atomic<size_t> stackCacheLimit;
    static size_t stackCacheHardLimit;

    static sl::Atomic<size_t> limitSampling;
    static size_t limitEwma;
    static size_t sampleDivider;
    static size_t sampleCalls;
    static size_t lastAllocated;

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
        const size_t now = shard.count.Load(sl::Relaxed) - count;

        shard.count.Store(now, sl::Relaxed);
        if (now < shard.lowCountSample)
            shard.lowCountSample = now;
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
        const auto cpuCount = MySystemDomain().smpControls.Size();

        const size_t initLimit = ReadConfigUint(
            "npk.vm.stack_cache_init_limit", DefaultStackCacheLimit);
        stackCacheHardLimit = sl::Max(initLimit, ReadConfigUint(
            "npk.vm.stack_cache_hard_limit", initLimit * 2));
        sampleDivider = sl::Max<size_t>(1, ReadConfigUint(
            "npk.vm.stack_cache_sample_divider", 1));

        stackCacheLimit.Store(initLimit, sl::Relaxed);
        limitEwma = initLimit << LimitFracBits;

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

        auto conv = sl::ConvertUnits(initLimit * KernelStackSize());
        Log("Kernel stack caches: init limit of %zu each (%zu.%zu %sB each),"
            " hard limit %zu", LogLevel::Info, initLimit, conv.major,
            conv.minor, conv.prefix, stackCacheHardLimit);

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

            const size_t limit = stackCacheLimit.Load(sl::Relaxed);
            while (cache.ready.count.Load(sl::Relaxed) > limit)
            {
                excess.PushBack(cache.ready.list.PopBack());
                SubCount(cache.ready, 1);
                excessCount++;
            }

            const size_t now = cache.ready.count.Load(sl::Relaxed);
            if (now > cache.ready.highCountSample)
                cache.ready.highCountSample = now;
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
        size_t* swing, sl::TimeCount timeout)
    {
        CachedStackList victims;

        cache.doomed.lock.Lock();
        while (!cache.doomed.list.Empty())
        {
            victims.PushBack(cache.doomed.list.PopFront());
            SubCount(cache.doomed, 1);
        }
        cache.doomed.lock.Unlock();

        auto& ready = cache.ready;

        ready.lock.Lock();
        if (swing != nullptr)
            *swing += ready.highCountSample - ready.lowCountSample;

        while (ready.count.Load(sl::Relaxed) > keep)
        {
            victims.PushBack(ready.list.PopBack());
            SubCount(ready, 1);
        }

        if (swing != nullptr || keep == 0)
        {
            ready.lowCountSample = ready.count.Load(sl::Relaxed);
            ready.highCountSample = ready.lowCountSample;
        }
        ready.lock.Unlock();

        if (victims.Empty())
            return 0;

        return FreeStackList(cache, victims, timeout);
    }

    static bool ShouldTakeSample()
    {
        size_t expected = 0;
        if (!limitSampling.CompareExchange(expected, 1, sl::Acquire))
            return false;

        sampleCalls++;
        if (sampleCalls % sampleDivider == 0)
            return true;

        limitSampling.Store(0, sl::Release);

        return false;
    }

    static void UpdateStackCacheLimit(size_t swing)
    {
        const size_t allocated = stacksAllocated.Load(sl::Relaxed);
        const size_t misses = allocated - lastAllocated;

        lastAllocated = allocated;
        const size_t need = sl::Min(((swing + misses) << LimitFracBits)
            / stackCaches.Size(), stackCacheHardLimit << LimitFracBits);

        if (need > limitEwma)
            limitEwma += (need - limitEwma) >> LimitEwmaShift;
        else
            limitEwma -= (limitEwma - need) >> LimitEwmaShift;

        const size_t half = (size_t)1 << (LimitFracBits - 1);
        const size_t floor = sl::Min<size_t>(1, stackCacheHardLimit);
        const size_t limit = sl::Clamp((limitEwma + half) >> LimitFracBits,
            floor, stackCacheHardLimit);

        stackCacheLimit.Store(limit, sl::Relaxed);
    }

    size_t Private::TrimKernelStackCaches(size_t keepPerCpu,
        sl::TimeCount timeout)
    {
        if (stackCaches.Empty())
            return 0;

        const bool sample = (keepPerCpu != 0) && ShouldTakeSample();
        const size_t keep = sl::Min(keepPerCpu,
            stackCacheLimit.Load(sl::Relaxed));

        size_t swing = 0;
        size_t* swingOut = nullptr;
        if (sample)
            swingOut = &swing;

        size_t freed = 0;
        for (size_t i = 0; i < stackCaches.Size(); i++)
            freed += TrimOneStackCache(stackCaches[i], keep, swingOut, timeout);

        if (sample)
        {
            UpdateStackCacheLimit(swing);
            limitSampling.Store(0, sl::Release);
        }

        return freed;
    }

    void GetStackCacheStats(StackCacheStats& out)
    {
        out = {};
        out.allocated = stacksAllocated.Load(sl::Relaxed);
        out.live = liveStacks.Load(sl::Relaxed);
        out.peak = peakStacks.Load(sl::Relaxed);
        out.limit = stackCacheLimit.Load(sl::Relaxed);
        out.hardLimit = stackCacheHardLimit;

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
