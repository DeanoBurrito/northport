#include <memory/Heap.h>
#include <memory/Pmm.h>
#include <arch/Platform.h>
#include <arch/Paging.h>
#include <debug/Log.h>
#include <Memory.h>

namespace Npk::Memory
{
/*
    This implementation can check for heap corruption due to nodes being ovewritten.
    It's pretty basic but it can help catch some (poor) bugs.
    Just undefine this macro below and recompile to enable it. It's hard on performance though
    so best to leave it disabled unless you're looking for a bug. By default these macros 
    are a no-op.
*/
// #define NP_HEAP_POOL_CHECKSUM

#ifdef NP_HEAP_POOL_CHECKSUM
    #define UPDATE_CHECKSUM(x) SetChecksum(x)
    #define VERIFY_CHECKSUM(x) ASSERT(ChecksumValid(x), "Heap checksum mismatch.")

    inline void SetChecksum(PoolNode* node)
    {
        const uintptr_t nodeAddr = (uintptr_t)node;
        node->checksum = 0;
        node->checksum |= ((uintptr_t)node->next - nodeAddr) << 48;
        node->checksum |= ((nodeAddr - (uintptr_t)node->prev) & 0xFFFF) << 32;
        node->checksum |= (node->size & 0xFFFF) << 16;
        node->checksum |= nodeAddr & 0xFFFF;
    }

    inline bool ChecksumValid(PoolNode* node)
    {
        if (node->checksum == 0)
            return false; //impossible, would imply weird conditions we cant meet in the kernel heap.
        const uintptr_t next = node->checksum >> 48;
        const uintptr_t prev = (node->checksum >> 32) & 0xFFFF;
        const uintptr_t size = (node->checksum >> 16) & 0xFFFF;
        const uintptr_t addr = node->checksum & 0xFFFF;

        const uintptr_t nodeAddr = (uintptr_t)node;
        if (next != (((uintptr_t)node->next - nodeAddr) & 0xFFFF))
            return false;
        if (prev != ((nodeAddr - (uintptr_t)node->prev) & 0xFFFF))
            return false;
        if (size != (node->size & 0xFFFF))
            return false;
        if (addr != ((uintptr_t)node & 0xFFFF))
            return false;
        return true;
    }
#else
    #define UPDATE_CHECKSUM(x)
    #define VERIFY_CHECKSUM(x)
#endif

    void PoolAlloc::MergePrev(PoolNode* node)
    {
        if (node->prev == nullptr)
            return;
        if ((uintptr_t)node->prev->Data() + node->prev->size != (uintptr_t)node)
            return;
        VERIFY_CHECKSUM(node)
        
        PoolNode* prev = node->prev;
        prev->next = node->next;
        if (node->next != nullptr)
        {
            node->next->prev = prev;
            UPDATE_CHECKSUM(node->next);
        }
        
        prev->size += node->size;
        prev->size += sizeof(PoolNode);
        UPDATE_CHECKSUM(prev);
    }

    void PoolAlloc::MergeNext(PoolNode* node)
    {
        if (node->next == nullptr)
            return;
        if ((uintptr_t)node + node->size != (uintptr_t)node->next)
            return;
        VERIFY_CHECKSUM(node)
        
        PoolNode* next = node->next;
        node->next = next->next;
        if (next->next != nullptr)
        {
            next->next->prev = node;
            UPDATE_CHECKSUM(next->next);
        }
        
        node->size += node->size;
        node->size += sizeof(PoolNode);
        UPDATE_CHECKSUM(node);
    }

    void PoolAlloc::Split(PoolNode* node, size_t spaceNeeded)
    {
        VERIFY_CHECKSUM(node);
        spaceNeeded = sl::AlignUp(spaceNeeded, sizeof(PoolNode));

        //only split if it leaves us space for another allocation: smaller regions will be allocated by the slabs.
        if (node->size <= minAllocSize + spaceNeeded + sizeof(PoolNode))
            return;
        
        const uintptr_t nextAddr = (uintptr_t)node->Data() + spaceNeeded;
        PoolNode* latest = new (reinterpret_cast<void*>(nextAddr)) PoolNode{ node, node->next, node->size, 0 };
        latest->size -= spaceNeeded;
        latest->size -= sizeof(PoolNode);
        ASSERT(latest->size > minAllocSize, "I am bad at maths.");
        
        if (node->next != nullptr)
        {
            node->next->prev = latest;
            UPDATE_CHECKSUM(node->next);
        }
        node->next = latest;
        if (node == tail)
            tail = latest;
        
        node->size = spaceNeeded;

        UPDATE_CHECKSUM(node);
        UPDATE_CHECKSUM(latest);
    }

    void PoolAlloc::Expand(size_t minSize)
    {
        const size_t pagesNeeded = sl::AlignUp(minSize, PageSize) / PageSize;
        for (size_t i = 0; i < pagesNeeded; i++)
            MapMemory(kernelMasterTables, regionTop + i * PageSize, PMM::Global().Alloc(), PageFlags::Write, PageSizes::_4K, false);
        kernelTablesGen++;

        if ((uintptr_t)tail->Data() + tail->size == regionTop)
            tail->size += pagesNeeded * PageSize;
        else
        {
            PoolNode* latest = new (reinterpret_cast<void*>(regionTop)) PoolNode{ tail, nullptr, pagesNeeded * PageSize, 0 };
            latest->size -= sizeof(PoolNode);
            tail->next = latest;
            UPDATE_CHECKSUM(tail);
            tail = latest;
        }
        UPDATE_CHECKSUM(tail);
        regionTop += pagesNeeded * PageSize;
    }

    void PoolAlloc::Init(uintptr_t start, size_t minAllocAmount)
    {
        sl::ScopedLock scopeLock(lock);

        regionBase = start;
        regionTop = start + PoolStartPages * PageSize;
        minAllocSize = minAllocAmount;

        //TODO: use VMM for heap mappings instead of accessing it directly like this
        for (size_t i = 0; i < PoolStartPages; i++)
            MapMemory(kernelMasterTables, regionBase + i * PageSize, PMM::Global().Alloc(), PageFlags::Write, PageSizes::_4K, false);
        kernelTablesGen++;
        
        start = sl::AlignUp(start, sizeof(PoolNode));
        head = tail = new((void*)start) PoolNode{ nullptr, nullptr, PoolStartPages * PageSize, 0 };
        head->size -= sizeof(PoolNode);
        UPDATE_CHECKSUM(head);
    }

    void* PoolAlloc::Alloc(size_t bytes)
    {
        sl::ScopedLock scopeLock(lock);
        VERIFY_CHECKSUM(head);
        VERIFY_CHECKSUM(tail);

        PoolNode* scan = head;
        while (scan != nullptr)
        {
            if (scan->size < bytes)
            {
                scan = scan->next;
                continue;
            }
            
            Split(scan, bytes);
            break;
        }

        if (scan == nullptr)
        {
            Expand(bytes);
            scan = tail;
            Split(scan, bytes);
        }
        
        if (scan->prev != nullptr)
        {
            scan->prev->next = scan->next;
            UPDATE_CHECKSUM(scan->prev);
        }
        if (scan->next != nullptr)
        {
            scan->next->prev = scan->prev;
            UPDATE_CHECKSUM(scan->next);
        }
        if (head == scan)
        {
            head = scan->next;
            UPDATE_CHECKSUM(head);
        }
        if (tail == scan)
        {
            tail = scan->prev;
            UPDATE_CHECKSUM(head);
        }
        
        sl::memset(scan->Data(), 0, scan->size);
        return scan->Data();
    }

    bool PoolAlloc::Free(void* ptr)
    {
        if ((uintptr_t)ptr < regionBase || (uintptr_t)ptr >= regionTop)
            return false;
        
        sl::ScopedLock scopeLock(lock);
        PoolNode* node = static_cast<PoolNode*>(ptr) - 1;
        VERIFY_CHECKSUM(node);
        VERIFY_CHECKSUM(head);
        VERIFY_CHECKSUM(tail);

        if ((uintptr_t)head < (uintptr_t)head)
        {
            head->prev = node;
            node->next = head;
            node->prev = nullptr;
            head = node;
            UPDATE_CHECKSUM(node);
            UPDATE_CHECKSUM(node->next);
            MergeNext(head);
            return true;
        }

        PoolNode* scan = head;
        while (scan != nullptr && (uintptr_t)node < (uintptr_t)scan)
            scan = scan->next;
        
        if (scan != nullptr)
        {
            node->next = scan->next;
            node->prev = scan;
            if (scan->next != nullptr)
                scan->next->prev = node;
            scan->next = node;
            UPDATE_CHECKSUM(node);
            UPDATE_CHECKSUM(node->prev);
            UPDATE_CHECKSUM(node->next);
            MergeNext(node);
            MergePrev(node);
            return true;
        }
        else
        {
            node->next = nullptr;
            node->prev = tail;
            tail->next = node;
            tail = node;
            UPDATE_CHECKSUM(node);
            UPDATE_CHECKSUM(node->prev);
            MergePrev(node);
            return true;
        }
    }
}
