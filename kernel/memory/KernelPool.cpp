#include <memory/KernelPool.h>
#include <memory/KernelHeap.h>
#include <memory/Paging.h>
#include <PlacementNew.h>
#include <Locks.h>
#include <Log.h>

namespace Kernel::Memory
{
    void KernelPool::TrySplit(KernelPoolNode* node, size_t allocSize)
    {
        if (node == nullptr)
            return;
        if (node->length <= allocSize + sizeof(KernelPoolNode))
            return;
        
        const size_t latestLength = node->length - sizeof(KernelPoolNode) - allocSize;
        KernelPoolNode* latest = new(sl::NativePtr(node->Data()).As<void>(allocSize)) KernelPoolNode(node, node->next, latestLength);

        latest->isFree = true;
        if (node->next)
            node->next->prev = latest;
        node->length = allocSize;
        node->next = latest;

        if (tail == node)
            tail = latest;

        usedBytes += sizeof(KernelPoolNode);
    }

    void KernelPool::TryMerge(KernelPoolNode* node)
    {
        if (node == nullptr)
            return;
        if (!node->isFree)
            return;
        
        //try merge forwards first
        if (node->next != nullptr && node->next->isFree)
        {
            KernelPoolNode* next = node->next;

            node->length += next->length + sizeof(KernelPoolNode);
            if (next->next != nullptr)
                next->next->prev = node;
            node->next = next->next;

            usedBytes -= sizeof(KernelPoolNode);
        }
        
        //then merge backwards
        if (node->prev != nullptr && node->prev->isFree)
        {
            KernelPoolNode* prev = node->prev;
            prev->length += node->length + sizeof(KernelPoolNode);
            if (node->next != nullptr)
                node->next->prev = prev;
            prev->next = node->next;

            usedBytes -= sizeof(KernelPoolNode);
        }
    }

    void KernelPool::Expand(size_t allocSize)
    {
        const size_t newPages = (allocSize - (tail->isFree ? tail->length : 0)) / PAGE_FRAME_SIZE + 1;
        if (newPages > 1)
            Logf("Kernel heap pool is expanding by %u pages.", LogSeverity::Warning, newPages);

        PageTableManager::Current()->MapRange(allocRegion.base.raw + allocRegion.length, newPages, MemoryMapFlags::AllowWrites);
        allocRegion.length += newPages * PAGE_FRAME_SIZE;

        if (tail->isFree)
            tail->length += newPages * PAGE_FRAME_SIZE;
        else
        {
            //add a new node after the tail
            KernelPoolNode* latest = new(sl::NativePtr(tail->Data()).As<void>(tail->length)) 
                KernelPoolNode(tail, nullptr, newPages * PAGE_FRAME_SIZE - sizeof(KernelPoolNode));
            tail->next = latest;
            latest->isFree = true;
            tail = latest;
        }
    }
    
    void KernelPool::Init(sl::NativePtr base)
    {
        allocRegion = { base, KernelHeapStartSize };
        usedBytes = 0;

        PageTableManager::Current()->MapRange(base, KernelHeapStartSize / PAGE_FRAME_SIZE, MemoryMapFlags::AllowWrites);
        tail = head = new (base.ptr) KernelPoolNode(nullptr, nullptr, KernelHeapStartSize - sizeof(KernelPoolNode));
        head->isFree = true;

        sl::SpinlockRelease(&lock);
    }

    void* KernelPool::Alloc(size_t size)
    { 
        sl::ScopedSpinlock scopeLock(&lock);
        
        KernelPoolNode* returnNode = nullptr;
        for (KernelPoolNode* scan = head; scan != nullptr; scan = scan->next)
        {
            if (!scan->isFree)
                continue;
            if (scan->length < size)
                continue;
            
            if (scan->length > size)
                TrySplit(scan, size);
            
            returnNode = scan;
            goto claim_node;
        }

        //if we've gotten to this point, there's no suitable nodes
        Expand(size);
        TrySplit(tail, size);
        if (tail->isFree && tail->length == size)
            returnNode = tail;
        else if (tail->prev->isFree && tail->prev->length == size)
            returnNode = tail->prev;
        if (returnNode != nullptr)
            goto claim_node;

        return nullptr;

claim_node:
        usedBytes += returnNode->length;
        returnNode->isFree = false;
        sl::memset(returnNode->Data(), 0, returnNode->length);
        return returnNode->Data();
    }

    bool KernelPool::Free(sl::NativePtr where)
    {
        if (where.raw < allocRegion.base.raw || where.raw > allocRegion.length + allocRegion.base.raw)
            return false;

        sl::ScopedSpinlock scopeLock(&lock);
        for (KernelPoolNode* scan = head; scan != nullptr; scan = scan->next)
        {
            if (scan->Data() != where.ptr)
                continue;
            
            //at this point scan contains our allocation
            if (scan->isFree)
            {
                Log("Attempted to free kpool memory already marked as free.", LogSeverity::Error);
                return true;
            }

            usedBytes -= scan->length;
            scan->isFree = true;
            TryMerge(scan);
            return true;
        }

        return false; 
    }

    void KernelPool::GetStats(HeapPoolStats& stats) const
    { 
        stats.totalSizeBytes = allocRegion.length;
        stats.usedBytes = usedBytes;

        stats.nodes = 0;
        KernelPoolNode* scan = head;
        while (scan != nullptr)
        {
            //NOTE: this is potentially invalid data since we arent locking (heap can be modified during scan)
            stats.nodes++;
            scan = scan->next;
        }
    }
}
