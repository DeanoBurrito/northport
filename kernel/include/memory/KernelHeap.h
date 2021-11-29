#pragma once

#include <stddef.h>
#include <Platform.h>
#include <NativePtr.h>

#define HEAP_ALLOC_ALIGN 0x10

namespace Kernel::Memory
{
    struct HeapNode
    {
        HeapNode* prev;
        HeapNode* next;
        size_t length;
        bool free;

        void CombineWithNext(HeapNode* last);
        void CarveOut(size_t allocSize, HeapNode* last);
    };
    
    class KernelHeap
    {
    private:
        HeapNode* head;
        HeapNode* tail;
        char lock; //TODO: do we need to lock on the whole class, or can we lock individual nodes?

    public:
        static KernelHeap* Global();

        void Init(sl::NativePtr baseAddress);

        void* Alloc(size_t size);
        void Free(void* ptr);
    };
}

FORCE_INLINE void* malloc(size_t size)
{
    //lmao
    return Kernel::Memory::KernelHeap::Global()->Alloc(size);
}

FORCE_INLINE void free(void* ptr)
{
    Kernel::Memory::KernelHeap::Global()->Free(ptr);
}
