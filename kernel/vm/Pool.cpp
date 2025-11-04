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

    static uintptr_t poolBase = 0;
    static size_t poolLength = 0;

    static Waitable nodesMutex {};
    static PoolNodeList freeNodes[Level1Count];
    static PoolNodeList usedNodes;

    static size_t GetL1Index(size_t len)
    {
        if (len >= MaxBucketSize)
            return Level1Count - 1;

        size_t index = len >> MinAllocBits;
        index &= (Level1Count - 1);

        return index;
    }

    //NOTE: assumes `nodesMutex` is held
    static void InsertNode(PoolNode* node)
    {
        const size_t l1Index = GetL1Index(node->length);

        auto& list = freeNodes[l1Index];
        list.InsertSorted(node, [](PoolNode* a, PoolNode* b) -> bool
            {
                return a->length < b->length;
            });
    }

    void* PoolAlloc(size_t len, HeapTag tag, sl::TimeCount timeout)
    {
        NPK_CHECK(tag != 0, nullptr);

        //enforce minimum size we try to allocate and determine array indices.
        len = sl::Max(MinAllocSize, len);
        size_t l1Index = GetL1Index(len);

        //try acquire the pool-space mutex
        WaitEntry entry {};
        auto status = WaitOne(&nodesMutex, &entry, timeout, NPK_WAIT_LOCATION);
        if (status != WaitStatus::Success)
            return nullptr;

        //1. try find a node of at least `len` bytes
        PoolNode* selected = nullptr;
        for (size_t i = l1Index; i < Level1Count; i++)
        {
            auto& level2 = freeNodes[i];
            if (level2.Empty())
                continue;

            for (auto it = level2.Begin(); it != level2.End(); ++it)
            {
                if (it->length < len)
                    continue;

                selected = &*it;
                break;
            }
            if (selected == nullptr)
                continue;

            level2.Remove(selected);
        }

        //2. if the node can hold at least `len + MinAllocSize` bytes, split
        //it and make the remaining free space available for use again.
        if (selected != nullptr 
            && selected->length >= MinAllocSize + len + HeaderOffset)
        {
            const uintptr_t addr = (uintptr_t)selected + len + HeaderOffset;
            PoolNode* next = new((void*)addr) PoolNode {};
            next->length = selected->length - (HeaderOffset + len);

            size_t index = (next->length >> MinAllocBits) & (Level1Count -1);
            if (next->length > MaxBucketSize)
                l1Index = Level1Count - 1;

            freeNodes[index].PushFront(next);
        }

        //release the pool-space mutex and we're done!
        SignalWaitable(&nodesMutex);

        if (selected == nullptr)
            return nullptr;
        const uintptr_t addr = reinterpret_cast<uintptr_t>(selected);
        return reinterpret_cast<void*>(addr + HeaderOffset);
    }

    bool PoolFree(void* ptr, size_t len, sl::TimeCount timeout)
    {
        const auto addr = reinterpret_cast<uintptr_t>(ptr);
        NPK_CHECK((addr & (MinAllocSize - 1)) == 0, false);

        auto* node = reinterpret_cast<PoolNode*>(
            reinterpret_cast<uintptr_t>(ptr) - HeaderOffset);

        NPK_CHECK(len <= node->length, false);

        WaitEntry entry {};
        auto status = WaitOne(&nodesMutex, &entry, timeout, NPK_WAIT_LOCATION);
        NPK_CHECK(status == WaitStatus::Success, false);

        node->tag = 0;
        InsertNode(node);
        //TODO: try coalesce neighbouring free regions based on the new node

        SignalWaitable(&nodesMutex);

        return true;
    }

    void InitPool(uintptr_t base, size_t length)
    {
        WaitEntry entry {};
        auto status = WaitOne(&nodesMutex, &entry, {}, NPK_WAIT_LOCATION);
        NPK_CHECK(status == WaitStatus::Success, );

        poolBase = sl::AlignUp(base, MinAllocSize);
        poolLength = length;

        auto page = AllocPage(false);
        auto ret = SetKernelMap(poolBase, LookupPagePaddr(page), VmFlag::Write);
        NPK_CHECK(ret == VmStatus::Success, );

        auto* node = new((void*)poolBase) PoolNode {};
        node->length = length - HeaderOffset;
        InsertNode(node);

        SignalWaitable(&nodesMutex);
    }
}

namespace Npk
{
    void* HeapAllocNonPaged(size_t len, HeapTag tag, sl::TimeCount timeout)
    {
        //TODO: slabs for small allocs and per-cpu caches (magazines)
        return Private::PoolAlloc(len, tag, timeout);
    }

    bool HeapFreeNonPaged(void* ptr, size_t len, sl::TimeCount timeout)
    {
        return Private::PoolFree(ptr, len, timeout);
    }

    void* HeapAlloc(size_t len, HeapTag tag, sl::TimeCount timeout)
    { NPK_UNREACHABLE(); }

    bool HeapFree(void* ptr, size_t len, sl::TimeCount timeout)
    { NPK_UNREACHABLE(); }
}
