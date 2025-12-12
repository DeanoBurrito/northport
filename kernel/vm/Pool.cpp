#include <VmPrivate.hpp>
#include <Maths.hpp>

namespace Npk::Private
{
    constexpr HeapTag FreeTag = ~0;

    constexpr size_t MinAllocBits = 6;
    constexpr size_t MinAllocSize = 1 << MinAllocBits;
    constexpr size_t Level1Bits = 8;
    constexpr size_t Level1Count = 1 << Level1Bits;
    constexpr size_t MaxBucketSize = MinAllocSize << Level1Bits;
    constexpr size_t WiredPoolDivisor = 4;
    constexpr size_t MetadataSizeDivisor = 8;

    struct Node
    {
        sl::ListHook hook;
        sl::ListHook addrHook;
        HeapTag tag;
        uintptr_t base;
        size_t length;
    };

    constexpr size_t HeaderOffset = sl::AlignUp(sizeof(Node), MinAllocSize);

    using PoolNodeList = sl::List<Node, &Node::hook>;
    using AddrNodeList = sl::List<Node, &Node::addrHook>;

    struct Pool
    {
        Waitable mutex {};
        AddrNodeList nodesByAddr;
        PoolNodeList freeNodes[Level1Count];

        PoolNodeList spareNodes;
        uintptr_t spareNodesHead;
        uintptr_t spareNodesMax;
        bool allowCoalescing;
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
    static Node* AllocNode(Pool& pool)
    {
        auto node = pool.spareNodes.PopFront();
        if (node != nullptr)
            return node;

        if (pool.spareNodesHead == pool.spareNodesMax)
            return nullptr;

        const auto page = AllocPage(true);
        if (page == nullptr)
            return nullptr;

        const auto paddr = LookupPagePaddr(page);
        const uintptr_t vbase = pool.spareNodesHead;
        pool.spareNodesHead += PageSize();

        auto result = SetKernelMap(vbase, paddr, VmFlag::Write);
        if (result != VmStatus::Success)
        {
            FreePage(page);
            pool.spareNodesHead -= PageSize();
            return nullptr;
        }

        const size_t nodeCount = PageSize() / sizeof(Node);
        for (size_t i = 0; i < nodeCount; i++)
        {
            const uintptr_t addr = vbase + i * sizeof(Node);
            auto node = new((void*)addr) Node {};

            pool.spareNodes.PushBack(node);
        }

        return pool.spareNodes.PopFront();
    }

    //NOTE: assumes `pool->nodesMutex` is held
    static void FreeNode(Pool& pool, Node* node)
    {
        pool.spareNodes.PushBack(node);
    }

    //NOTE: assumes `pool->nodesMutex` is held
    static void InsertFreeNode(Pool& pool, Node* node)
    {
        const size_t l1Index = GetL1Index(node->length);
        auto& list = pool.freeNodes[l1Index];

        list.InsertSorted(node, [](Node* a, Node* b) -> bool
            {
                return a->length < b->length;
            });
    }

    //NOTE: assumes `pool->nodesMutex` is held
    static bool TrySplitNode(Pool& pool, Node* node, size_t len)
    {
        if (node->length < len + MinAllocSize)
            return false;

        auto* next = AllocNode(pool);
        if (next == nullptr)
            return false;

        next->base = node->base + len;
        next->length = node->length - len;
        next->tag = FreeTag;
        node->length = len;

        pool.nodesByAddr.InsertSorted(next, [](Node* a, Node* b) -> bool
            {
                return a->base < b->base;
            });

        InsertFreeNode(pool, next);

        return true;
    }

    void* PoolAlloc(size_t len, HeapTag tag, bool paged, sl::TimeCount timeout)
    {
        NPK_CHECK(tag != 0, nullptr);

        len = sl::AlignUp(len, MinAllocSize);
        const size_t l1Index = GetL1Index(len);
        auto& pool = paged ? pagedPool : wiredPool;

        //try acquire the mutex for this pool
        if (!AcquireMutex(&pool.mutex, timeout, NPK_WAIT_LOCATION))
            return nullptr;

        //find a node with enough space
        Node* selected = nullptr;
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

        if (selected != nullptr)
            TrySplitNode(pool, selected, len);

        uintptr_t mapBegin = AlignDownPage(selected->base);
        uintptr_t mapEnd = AlignUpPage(selected->base + selected->length);

        const auto prev = pool.nodesByAddr.Before(selected);
        if (prev != pool.nodesByAddr.End() && prev->tag != FreeTag)
        {
            mapBegin = sl::Max(mapBegin, prev->base + prev->length);
            mapBegin = AlignUpPage(mapBegin);
        }

        const auto next = pool.nodesByAddr.After(selected);
        if (next != pool.nodesByAddr.End() && next->tag != FreeTag)
        {
            mapEnd = sl::Min(mapEnd, next->base);
            mapEnd = AlignDownPage(mapEnd);
        }

        for (uintptr_t i = mapBegin; i < mapEnd; i += PageSize())
        {
            auto page = AllocPage(false);
            NPK_ASSERT(page != nullptr);

            auto paddr = LookupPagePaddr(page);
            auto result = SetKernelMap(i, paddr, VmFlag::Write);
            NPK_ASSERT(result == VmStatus::Success);

            //TODO: if paged pool, add this page to the active lists
        }

        ReleaseMutex(&pool.mutex);

        if (selected == nullptr)
            return nullptr;

        selected->tag = tag;
        const uintptr_t addr = reinterpret_cast<uintptr_t>(selected);

        return reinterpret_cast<void*>(addr + HeaderOffset);
    }
    
    static Node* TryCoalesce(Pool& pool, Node* node)
    {
        auto before = pool.nodesByAddr.Before(node);
        if (before->tag == FreeTag)
        {
            const size_t l1Index = GetL1Index(before->length);
            auto beforeNode = &*before;
            auto& level2 = pool.freeNodes[l1Index];
            level2.Remove(beforeNode);

            pool.nodesByAddr.Remove(node);
            beforeNode->length += node->length;
            FreeNode(pool, node);

            node = beforeNode;
        }

        auto after = pool.nodesByAddr.After(node);
        if (after->tag == FreeTag)
        {
            const size_t l1Index = GetL1Index(after->length);
            auto afterNode = &*after;
            auto& level2 = pool.freeNodes[l1Index];
            level2.Remove(afterNode);

            pool.nodesByAddr.Remove(afterNode);
            node->length += after->length;
            FreeNode(pool, afterNode);
        }

        return node;
    }

    bool PoolFree(void* ptr, size_t len, HeapTag tag, bool paged, 
        sl::TimeCount timeout)
    {
        (void)len;

        const auto addr = reinterpret_cast<uintptr_t>(ptr);
        NPK_CHECK((addr & (MinAllocSize - 1)) == 0, false);

        Pool& pool = paged ? pagedPool : wiredPool;
        if (!AcquireMutex(&pool.mutex, timeout, NPK_WAIT_LOCATION))
            return false;

        Node* node = nullptr;
        for (auto it = pool.nodesByAddr.Begin(); it != pool.nodesByAddr.End();
            ++it)
        {
            if (addr < it->base)
                break;

            if (addr >= it->base && addr < it->base + it->length)
            {
                node = &*it;
                break;
            }
        }

        if (node != nullptr && node->tag == tag)
        {
            uintptr_t unmapBegin = AlignDownPage(node->base);
            uintptr_t unmapEnd = AlignUpPage(node->base + node->length);

            const auto prev = pool.nodesByAddr.Before(node);
            if (prev != pool.nodesByAddr.End() && prev->tag != FreeTag)
            {
                unmapBegin = sl::Max(unmapBegin, prev->base + prev->length);
                unmapBegin = AlignUpPage(unmapBegin);
            }

            const auto next = pool.nodesByAddr.After(node);
            if (next != pool.nodesByAddr.End() && next->tag != FreeTag)
            {
                unmapEnd = sl::Min(unmapEnd, next->base);
                unmapEnd = AlignDownPage(unmapEnd);
            }

            for (uintptr_t i = unmapBegin; i < unmapEnd; i += PageSize())
            {
                Paddr paddr;
                auto result = ClearKernelMap(i, &paddr);
                NPK_ASSERT(result == VmStatus::Success);

                FreePage(LookupPageInfo(paddr));
            }

            node->tag = FreeTag;
            if (pool.allowCoalescing)
                node = TryCoalesce(pool, node);

            InsertFreeNode(pool, node);
        }
        else if (node != nullptr && node->tag == FreeTag)
        {
            Log("Double free of %s-pool pointer: %p", LogLevel::Error, 
                paged ? "paged" : "wired", ptr);
        }
        else if (node != nullptr && node->tag != tag)
        {
            Log("Bad tag for free of %s-pool pointer: %p", LogLevel::Error, 
                paged ? "paged" : "wired", ptr);
        }
        else
        {
            Log("Failed to free %s-pool pointer: %p", LogLevel::Error, 
                paged ? "paged" : "wired", ptr);
        }
        ReleaseMutex(&pool.mutex);

        return true;
    }

    static void InitSpecificPool(Pool& pool, uintptr_t base, size_t length)
    {
        const size_t metadataLen = AlignUpPage(length / MetadataSizeDivisor);
        pool.spareNodesHead = base;
        pool.spareNodesMax = metadataLen;

        length -= metadataLen;
        base += metadataLen;

        auto* node = AllocNode(pool);
        NPK_ASSERT(node != nullptr);

        node->base = base;
        node->length = length;
        node->tag = FreeTag;

        InsertFreeNode(pool, node);
        pool.nodesByAddr.PushFront(node);
    }

    void InitPool(uintptr_t base, size_t length)
    {
        ResetWaitable(&wiredPool.mutex, WaitableType::Mutex, 1);
        ResetWaitable(&pagedPool.mutex, WaitableType::Mutex, 1);

        base = AlignUpPage(base);
        length = AlignDownPage(length);

        const uintptr_t wiredBase = base;
        const size_t wiredLength = AlignUpPage(length / WiredPoolDivisor);
        const uintptr_t pagedBase = wiredBase + wiredLength;
        const size_t pagedLength = length - wiredLength;
        const bool defaultCoalesce = ReadConfigUint("npk.vm.pool_coalescing", 
            false);

        NPK_ASSERT(AcquireMutex(&wiredPool.mutex, {}, NPK_WAIT_LOCATION));

        wiredPool.allowCoalescing = 
            ReadConfigUint("npk.vm.wired_pool_coalescing", defaultCoalesce);
        InitSpecificPool(wiredPool, wiredBase, wiredLength);
        ReleaseMutex(&wiredPool.mutex);

        NPK_ASSERT(AcquireMutex(&pagedPool.mutex, {}, NPK_WAIT_LOCATION));

        pagedPool.allowCoalescing = 
            ReadConfigUint("npk.vm.paged_pool_coalescing", defaultCoalesce);
        InitSpecificPool(pagedPool, pagedBase, pagedLength);
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

    bool HeapFreeNonPaged(void* ptr, size_t len, HeapTag tag, 
        sl::TimeCount timeout)
    {
        return Private::PoolFree(ptr, len, tag, false, timeout);
    }

    void* HeapAlloc(size_t len, HeapTag tag, sl::TimeCount timeout)
    {
        return Private::PoolAlloc(len, tag, true, timeout);
    }

    bool HeapFree(void* ptr, size_t len, HeapTag tag, sl::TimeCount timeout)
    {
        return Private::PoolFree(ptr, len, tag, true, timeout);
    }
}
