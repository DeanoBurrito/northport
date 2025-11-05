#include <VmPrivate.hpp>
#include <Maths.hpp>

namespace Npk::Private
{
    constexpr size_t MinAllocBits = 4;
    constexpr size_t MinAllocSize = 1 << MinAllocBits;
    constexpr size_t Level1Bits = 8;
    constexpr size_t Level1Count = 1 << Level1Bits;
    constexpr size_t MaxBucketSize = MinAllocSize << Level1Bits;

    struct PoolNode
    {
        sl::ListHook hook;
        HeapTag tag;
        size_t length;
    };

    constexpr size_t HeaderOffset = sl::AlignUp(sizeof(PoolNode), MinAllocSize);

    using PoolNodeList = sl::List<PoolNode, &PoolNode::hook>;

    struct Pool
    {
        Waitable mutex {};
        PoolNodeList usedNodes;
        PoolNodeList freeNodes[Level1Count];
    };

    static Pool wiredPool;
    static Pool pagedPool;

    static size_t GetL1Index(size_t len)
    {
        if (len >= MaxBucketSize)
            return Level1Count - 1;

        size_t index = len >> MinAllocBits;
        index &= (Level1Count - 1);

        return index;
    }

    //NOTE: assumes `pool->nodesMutex` is held
    static void InsertNode(Pool& pool, PoolNode* node)
    {
        const size_t l1Index = GetL1Index(node->length);
        auto& list = pool.freeNodes[l1Index];

        list.InsertSorted(node, [](PoolNode* a, PoolNode* b) -> bool
            {
                return a->length < b->length;
            });
    }

    void* PoolAlloc(size_t len, HeapTag tag, bool paged, sl::TimeCount timeout)
    {
        NPK_CHECK(tag != 0, nullptr);

        //enforce minimum size we try to allocate and determine array indices.
        len = sl::Max(MinAllocSize, len);
        const size_t l1Index = GetL1Index(len);

        auto& pool = paged ? pagedPool : wiredPool;

        //try acquire the mutex for this pool
        if (!AcquireMutex(&pool.mutex, timeout, NPK_WAIT_LOCATION))
            return nullptr;

        //find a node with enough space
        PoolNode* selected = nullptr;
        for (size_t i = l1Index; i < Level1Count; i++)
        {
            auto& level2 = pool.freeNodes[i];

            if (level2.Empty())
                continue;

            for (auto it = level2.Begin(); it != level2.End(); ++it)
            {
                if (it->length < len)
                    continue;

                selected = &*it;
                break;
            }

            if (selected != nullptr)
            {
                level2.Remove(selected);
                break;
            }
        }

        //if node is too big, try split it and make unused free space available.
        if (selected != nullptr
            && selected->length >= MinAllocSize + len + HeaderOffset)
        {
            const uintptr_t addr = (uintptr_t)selected + len + HeaderOffset;
            PoolNode* next = new((void*)addr) PoolNode {};
            next->length = selected->length - (HeaderOffset + len);

            InsertNode(pool, next);
        }

        ReleaseMutex(&pool.mutex);

        if (selected == nullptr)
            return nullptr;
        const uintptr_t addr = reinterpret_cast<uintptr_t>(selected);
        return reinterpret_cast<void*>(addr + HeaderOffset);
    }

    bool PoolFree(void* ptr, size_t len, bool paged, sl::TimeCount timeout)
    {
        const auto addr = reinterpret_cast<uintptr_t>(ptr);
        NPK_CHECK((addr & (MinAllocSize - 1)) == 0, false);

        auto* node = reinterpret_cast<PoolNode*>(
            reinterpret_cast<uintptr_t>(ptr) - HeaderOffset);
        NPK_CHECK(len <= node->length, false);

        Pool& pool = paged ? pagedPool : wiredPool;

        if (!AcquireMutex(&pool.mutex, timeout, NPK_WAIT_LOCATION))
            return false;

        node->tag = 0;
        InsertNode(pool, node);
        //TODO: coalesce with adjacent blocks if possible
        ReleaseMutex(&pool.mutex);

        return true;
    }

    void InitPool(uintptr_t base, size_t length)
    {
        ResetWaitable(&wiredPool.mutex, WaitableType::Mutex, 1);
        ResetWaitable(&pagedPool.mutex, WaitableType::Mutex, 1);

        const uintptr_t top = base + length;
        base = sl::AlignUp(base, MinAllocSize);
        length = sl::AlignDown(top - base, MinAllocSize);

        const uintptr_t wiredBase = base;
        const size_t wiredLength = length / 4;
        const uintptr_t pagedBase = wiredBase + wiredLength;
        const size_t pagedLength = length - wiredLength;

        //initialize non-paged/wired pool
        NPK_ASSERT(AcquireMutex(&wiredPool.mutex, {}, NPK_WAIT_LOCATION));

        auto page = AllocPage(false);
        auto ret = SetKernelMap(wiredBase, LookupPagePaddr(page), 
            VmFlag::Write);
        NPK_ASSERT(ret == VmStatus::Success);

        auto* node = new((void*)wiredBase) PoolNode {};
        node->length = wiredLength - HeaderOffset;
        InsertNode(wiredPool, node);

        ReleaseMutex(&wiredPool.mutex);

        //initialize pagable pool
        NPK_ASSERT(AcquireMutex(&pagedPool.mutex, {}, NPK_WAIT_LOCATION));
        
        //TODO: add page to domain->listLists.active
        page = AllocPage(false);
        ret = SetKernelMap(pagedBase, LookupPagePaddr(page), VmFlag::Write);
        NPK_ASSERT(ret == VmStatus::Success);

        node = new((void*)pagedBase) PoolNode {};
        node->length = pagedLength - HeaderOffset;
        InsertNode(pagedPool, node);

        ReleaseMutex(&pagedPool.mutex);
    }
}

namespace Npk
{
    void* HeapAllocNonPaged(size_t len, HeapTag tag, sl::TimeCount timeout)
    {
        //TODO: slabs for small allocs and per-cpu caches (magazines)
        return Private::PoolAlloc(len, tag, false, timeout);
    }

    bool HeapFreeNonPaged(void* ptr, size_t len, sl::TimeCount timeout)
    {
        return Private::PoolFree(ptr, len, false, timeout);
    }

    void* HeapAlloc(size_t len, HeapTag tag, sl::TimeCount timeout)
    {
        return Private::PoolAlloc(len, tag, true, timeout);
    }

    bool HeapFree(void* ptr, size_t len, sl::TimeCount timeout)
    {
        return Private::PoolFree(ptr, len, true, timeout);
    }
}
