#pragma once

#include <stdint.h>
#include <stddef.h>
#include <Locks.h>

namespace Npk::Memory
{
    constexpr size_t PoolStartPages = 16;
    constexpr size_t PoolExpandFactor = 2;

    struct PoolNode
    {
        PoolNode* prev;
        PoolNode* next;
        size_t size;
        uintptr_t checksum;

        [[gnu::always_inline]]
        inline void* Data()
        { return reinterpret_cast<void*>(this + 1); }
    };
    
    class PoolAlloc
    {
    private:
        uintptr_t regionBase;
        uintptr_t regionTop;
        PoolNode* head;
        PoolNode* tail;
        sl::TicketLock lock;
        size_t minAllocSize;

        void MergePrev(PoolNode* node);
        void MergeNext(PoolNode* node);
        void Split(PoolNode* node, size_t spaceNeeded);
        void Expand(size_t minSize);

    public:
        PoolAlloc() = default;
        PoolAlloc(const PoolAlloc&) = delete;
        PoolAlloc& operator=(const PoolAlloc&) = delete;
        PoolAlloc(PoolAlloc&&) = delete;
        PoolAlloc& operator=(PoolAlloc&&) = delete;

        void Init(uintptr_t start, size_t minAllocAmount);

        void* Alloc(size_t bytes);
        bool Free(void* ptr);
    };
}
