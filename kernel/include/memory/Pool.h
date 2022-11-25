#pragma once

#include <stdint.h>
#include <stddef.h>
#include <Locks.h>

namespace Npk::Memory
{
    constexpr size_t PoolMinExpansionScale = 2;
    
    struct PoolRegion;

    struct PoolNode
    {
        PoolNode* prev;
        PoolNode* next;
        PoolRegion* parent;
        size_t length;

        [[gnu::always_inline]]
        inline void* Data()
        { return reinterpret_cast<void*>(this + 1); }
    };

    struct PoolRegion
    {
        uintptr_t base;
        size_t length;
        PoolNode* first;
        PoolNode* last;
        PoolRegion* prev;
        PoolRegion* next;
        sl::TicketLock lock;
    };

    class PoolAlloc
    {
    private:
        PoolRegion* head;
        PoolRegion* tail;
        size_t minAllocSize;
        sl::TicketLock listLock;
        bool pinned;

        void MergePrev(PoolNode* node);
        void MergeNext(PoolNode* node);
        void Split(PoolNode* node, size_t spaceNeeded);
        void Expand(size_t minSize, bool takeLock);

    public:
        PoolAlloc() = default;
        PoolAlloc(const PoolAlloc&) = delete;
        PoolAlloc& operator=(const PoolAlloc&) = delete;
        PoolAlloc(PoolAlloc&&) = delete;
        PoolAlloc& operator=(PoolAlloc&&) = delete;

        void Init(size_t minAllocBytes, bool isPinned);

        void* Alloc(size_t bytes);
        bool Free(void* ptr);
    };
}
