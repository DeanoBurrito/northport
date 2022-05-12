#pragma once

#include <NativePtr.h>
#include <BufferView.h>

namespace Kernel::Memory
{
    struct HeapPoolStats
    {
        sl::NativePtr base;
        size_t totalSizeBytes;
        size_t usedBytes;
        size_t nodes;
    };

    struct KernelPoolNode
    {
        bool isFree;
        KernelPoolNode* prev;
        KernelPoolNode* next;
        //length refers to the data owned by this node, not including the header (this struct)
        size_t length;

        KernelPoolNode(KernelPoolNode* prev, KernelPoolNode* next, size_t len)
        : isFree(false), prev(prev), next(next), length(len)
        {}

        [[gnu::always_inline]] inline
        void* Data()
        { return this + 1;}
    };
    
    class KernelPool
    {
    private:
        sl::BufferView allocRegion;
        size_t usedBytes;
        KernelPoolNode* head;
        KernelPoolNode* tail;
        char lock;

        //NOTE: these functions assume they're called with the pool already locked.
        void TrySplit(KernelPoolNode* node, size_t allocSize);
        void TryMerge(KernelPoolNode* node);
        void Expand(size_t allocSize);

    public:
        void Init(sl::NativePtr base);

        void* Alloc(size_t size);
        bool Free(sl::NativePtr where);

        void GetStats(HeapPoolStats& stats) const;
    };
}
