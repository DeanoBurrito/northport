#include <memory/PhysicalMemory.h>
#include <memory/Paging.h>
#include <memory/KernelHeap.h>
#include <Log.h>
#include <Locks.h>

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
    
    bool HeapNode::CombineWithNext(KernelHeap& heap)
    {
        if (!free)
            return false;
        if (next == nullptr)
            return false;
        if (!next->free)
            return false;

        if (heap.tail == next)
            heap.tail = this;

        length += next->length + sizeof(HeapNode);
        next = next->next;
        if (next)
            next->prev = this;

#ifdef NORTHPORT_DEBUG_USE_HEAP_CANARY
        SetCanary(this);
        if (next)
            SetCanary(next);
#endif
        return true;
    }

    void HeapNode::CarveOut(size_t allocSize, KernelHeap& heap)
    {
        const size_t actualSize = sizeof(HeapNode) + allocSize;
        if (length - actualSize > length)
            return;

        HeapNode* nextNode = reinterpret_cast<HeapNode*>((uint64_t)this + actualSize);
        nextNode->free = this->free;
        nextNode->next = this->next;
        nextNode->prev = this;
        if (next)
            next->prev = nextNode;
        next = nextNode;
        nextNode->length = length - actualSize;
        length = allocSize;

        if (heap.tail == this)
            heap.tail = nextNode;

#ifdef NORTHPORT_DEBUG_USE_HEAP_CANARY
        SetCanary(nextNode);
        SetCanary(this);
        if (nextNode->next)
            SetCanary(nextNode->next);
#endif
    }

    void KernelHeap::ExpandHeap(size_t nextAllocSize)
    {
        //reached end of heap, seems there's not enough space. Lets allocate what we need and return from that.
        const size_t requiredPages = (sizeof(HeapNode) + nextAllocSize) / PAGE_FRAME_SIZE + 1;
        uint64_t endOfHeapAddr = (uint64_t)tail + tail->length + sizeof(HeapNode);

        if (requiredPages > 1)
            Log("Kernel heap is expanding by more than 1 page.", LogSeverity::Warning);
        PageTableManager::Current()->MapRange(endOfHeapAddr, requiredPages, MemoryMapFlags::AllowWrites);
        tail->next = reinterpret_cast<HeapNode*>(endOfHeapAddr);

        tail->next->next = nullptr;
        tail->next->prev = tail;
        tail->next->free = true;
        tail->next->length = requiredPages * PAGE_FRAME_SIZE - sizeof(HeapNode);

        tail = tail->next;

#ifdef NORTHPORT_DEBUG_USE_HEAP_CANARY
        SetCanary(tail->prev);
        SetCanary(tail);
#endif
    }

    KernelHeap globalKernelHeap;
    KernelHeap* KernelHeap::Global()
    { return &globalKernelHeap; }

    void KernelHeap::Init(sl::NativePtr base)
    {
        sl::ScopedSpinlock scopeLock(&lock);

        head = base.As<HeapNode>();
        PageTableManager::Current()->MapMemory(head, MemoryMapFlags::AllowWrites);

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
        
        InterruptLock intLock; //TODO: a better solution than disabling interrupts every alloc()
        sl::ScopedSpinlock scopeLock(&lock);

        //align alloc size
        size = (size / HEAP_ALLOC_ALIGN + 1) * HEAP_ALLOC_ALIGN;
        
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
                if (scan == tail)
                    ExpandHeap(size);
                    
                continue;
            }

            if (scan->length == size)
            {
                scan->free = false;
                bytesUsed += size;
                return (void*)((uint64_t)scan + sizeof(HeapNode));
            }

            if (scan->length > size)
            {
                if (scan->length - (size + sizeof(HeapNode)) > HEAP_ALLOC_ALIGN)
                    scan->CarveOut(size, *this); //carve out space for the next node, ONLY if subdividing would be useful
                
                scan->free = false;
                bytesUsed += size + sizeof(HeapNode);
                return (void*)((uint64_t)scan + sizeof(HeapNode));
            }

            if (scan == tail)
                ExpandHeap(size);

            scan = scan->next;
        }

        Log("Kernel heap could not allocate for requested size.", LogSeverity::Error);
        return nullptr;
    }

    void KernelHeap::Free(void* ptr)
    {
        if (ptr == nullptr)
            return;
        
        InterruptLock intLock;
        sl::ScopedSpinlock scopeLock(&lock);

        sl::NativePtr where(ptr);
        where.raw -= sizeof(HeapNode);
        if (where.raw < (uint64_t)head || where.raw >= (uint64_t)tail)
        {
            Log("Attempted to free memory not in kernel heap.", LogSeverity::Error);
            return;
        }

        HeapNode* heapNode = where.As<HeapNode>();
        heapNode->free = true;
        bytesUsed -= heapNode->length;

        if (heapNode->CombineWithNext(*this))
            bytesUsed -= sizeof(HeapNode);
        if (heapNode->prev)
            heapNode->prev->CombineWithNext(*this);
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
