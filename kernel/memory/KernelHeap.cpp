#include <memory/PhysicalMemory.h>
#include <memory/Paging.h>
#include <memory/KernelHeap.h>
#include <Log.h>

#ifdef NORTHPORT_DEBUG_USE_HEAP_CANARY
    #pragma message "Kernel heap is compiling with canary built in. Useful for debugging, but can cause slowdowns."
#endif

namespace Kernel::Memory
{
#ifdef NORTHPORT_DEBUG_USE_HEAP_CANARY
    bool CheckCanary(HeapNode* node)
    { 
        // return node->canary == (uint64_t)node;
        return node->canary == ((uint64_t)node->prev << 32 | ((uint64_t)node->next & 0xFFFF'FFFF));
    }

    void SetCanary(HeapNode* node)
    {
        // node->canary = (uint64_t)node;
        node->canary = ((uint64_t)node->prev << 32 | ((uint64_t)node->next & 0xFFFF'FFFF));
    }
#endif
    
    void HeapNode::CombineWithNext(HeapNode* last)
    {
        if (!free)
            return;
        if (next == nullptr)
            return;
        if (!next->free)
            return;

        if (next == last)
            last = this;

        length += next->length + sizeof(HeapNode);
        next = next->next;

#ifdef NORTHPORT_DEBUG_USE_HEAP_CANARY
        SetCanary(this);
#endif
    }

    void HeapNode::CarveOut(size_t allocSize, HeapNode* last)
    {
        const size_t actualSize = sizeof(HeapNode) + allocSize;
        HeapNode* nextNode = reinterpret_cast<HeapNode*>((uint64_t)this + actualSize);
        nextNode->free = this->free;
        nextNode->next = this->next;
        nextNode->prev = this;
        if (next)
            next->prev = nextNode;
        next = nextNode;
        nextNode->length = length - actualSize;
        length = allocSize;

        if (this == last)
            last = nextNode;

#ifdef NORTHPORT_DEBUG_USE_HEAP_CANARY
        SetCanary(nextNode);
        SetCanary(this);
#endif
    }

    KernelHeap globalKernelHeap;
    KernelHeap* KernelHeap::Global()
    { return &globalKernelHeap; }

    void KernelHeap::Init(sl::NativePtr base)
    {
        ScopedSpinlock scopeLock(&lock);

        head = base.As<HeapNode>();
        PageTableManager::Local()->MapMemory(head, MemoryMapFlag::AllowWrites);

        tail = head;
        head->next = head->prev = nullptr;
        head->length = (size_t)PagingSize::Physical - sizeof(HeapNode);
        head->free = true;

#ifdef NORTHPORT_DEBUG_USE_HEAP_CANARY
        SetCanary(head);
        Log("Kernel heap is using debug canary.", LogSeverity::Verbose);
#endif
    }

    void* KernelHeap::Alloc(size_t size)
    {
        if (size == 0)
            return nullptr;
        
        ScopedSpinlock scopeLock(&lock);

        //align alloc size
        size = ((size  / HEAP_ALLOC_ALIGN) + 1) * HEAP_ALLOC_ALIGN;
        const size_t requestedSize = size + sizeof(HeapNode);
        
        HeapNode* scan = head;
        while (scan != nullptr)
        {
#ifdef NORTHPORT_DEBUG_USE_HEAP_CANARY
            if (!CheckCanary(scan))
                Log("Kernel heap canary value is corrupted. Oh no.", LogSeverity::Error);
#endif
            
            if (!scan->free)
            {
                scan = scan->next;
                continue;
            }

            if (scan->length >= requestedSize)
            {
                if (scan->length > requestedSize + HEAP_ALLOC_ALIGN)
                    scan->CarveOut(size, tail);
                if (tail == scan && scan->next != nullptr)
                    tail = scan->next;

                scan->free = false;
                return (void*)((uint64_t)scan + sizeof(HeapNode));
            }

            if (scan == tail)
            {
                //reached end of heap, seems there's not enough space. Lets allocate what we need and return from that.
                const size_t requiredPages = requestedSize / PAGE_FRAME_SIZE + 1;
                uint64_t endOfHeapAddr = (uint64_t)tail + tail->length + sizeof(HeapNode);

                if (requiredPages > 1)
                    Log("VMM::MapRange() not yet implemented! //TODO:", LogSeverity::Fatal);
                PageTableManager::Local()->MapMemory(endOfHeapAddr, MemoryMapFlag::AllowWrites);
                scan->next = reinterpret_cast<HeapNode*>(endOfHeapAddr);

                scan->next->next = nullptr;
                scan->next->prev = scan;
                scan->next->free = true;
                scan->next->length = requiredPages * PAGE_FRAME_SIZE - sizeof(HeapNode);

                tail = scan->next;

#ifdef NORTHPORT_DEBUG_USE_HEAP_CANARY
                SetCanary(scan);
                SetCanary(tail);
#endif

            }

            scan = scan->next;
        }

        Log("Kernel heap could not allocate for requested size.", LogSeverity::Error);
        return nullptr;
    }

    void KernelHeap::Free(void* ptr)
    {
        if (ptr == nullptr)
            return;
        
        ScopedSpinlock scopeLock(&lock);

        sl::NativePtr where(ptr);
        where.raw -= sizeof(HeapNode);
        if (where.raw < (uint64_t)head || where.raw >= (uint64_t)tail)
        {
            Log("Attempted to free memory not in kernel heap.", LogSeverity::Error);
            return;
        }

        HeapNode* heapNode = where.As<HeapNode>();
        heapNode->free = true;

        heapNode->CombineWithNext(tail);
        if (heapNode->prev)
            heapNode->prev->CombineWithNext(tail);
    }
}

void* malloc(size_t size)
{
    return Kernel::Memory::KernelHeap::Global()->Alloc(size);
}

void free(void* ptr)
{
    Kernel::Memory::KernelHeap::Global()->Free(ptr);
}
