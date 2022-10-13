#include <memory/Pool.h>
#include <memory/Pmm.h>
#include <memory/Vmm.h>
#include <arch/Platform.h>
#include <debug/Log.h>
#include <Memory.h>

namespace Npk::Memory
{
    void PoolAlloc::MergePrev(PoolNode* node)
    {
        if (node->prev == nullptr)
            return;
        if ((uintptr_t)node->prev->Data() + node->prev->length != (uintptr_t)node)
            return;
        
        PoolNode* prev = node->prev;
        prev->next = node->next;
        if (node->next != nullptr)
            node->next->prev = prev;
        else
            node->parent->last = prev;
        
        prev->length += node->length;
        prev->length += sizeof(PoolNode);
    }

    void PoolAlloc::MergeNext(PoolNode* node)
    {
        if (node->next == nullptr)
            return;
        if ((uintptr_t)node + node->length != (uintptr_t)node->next)
            return;
        
        PoolNode* next = node->next;
        node->next = next->next;
        if (next->next != nullptr)
            next->next->prev = node;
        else
            node->parent->last = node;
        
        node->length += next->length;
        node->length += sizeof(PoolNode);
    }

    void PoolAlloc::Split(PoolNode* node, size_t spaceNeeded)
    {
        spaceNeeded = sl::AlignDown(spaceNeeded, sizeof(PoolNode));

        if (node->length <= minAllocSize + spaceNeeded + sizeof(PoolNode))
            return;
        
        const uintptr_t nextAddr = (uintptr_t)node->Data() + spaceNeeded;
        PoolNode* latest = new (reinterpret_cast<void*>(nextAddr)) PoolNode();
        latest->next = node->next;
        latest->prev = node;
        latest->parent = node->parent;
        latest->length = node->length - (spaceNeeded + sizeof(PoolNode));

        if (node->next != nullptr)
            node->next->prev = latest;
        else
            node->parent->last = latest;
        node->next = latest;
        node->length = spaceNeeded;
    }

    void PoolAlloc::Expand(size_t minSize, bool takeLock)
    {
        const size_t expandSize = sl::Max(minSize, minAllocSize * PoolMinExpansionScale) + sizeof(PoolRegion) + sizeof(PoolNode);
        auto maybeRegion = VMM::Kernel().Alloc(expandSize, 1, VmFlags::Anon | VmFlags::Write);
        ASSERT(maybeRegion, "Failed to expand kernel pool, VMM alloc failed.");

        PoolRegion* region = new((void*)maybeRegion->base) PoolRegion();
        if (takeLock)
            region->lock.Lock();
        region->base = sl::AlignUp(maybeRegion->base + sizeof(PoolRegion), sizeof(PoolNode));
        region->length = maybeRegion->base + maybeRegion->length - region->base;
        
        region->first = region->last = new((void*)region->base) PoolNode();
        region->first->next = region->first->prev = nullptr;
        region->first->parent = region;
        region->first->length = region->length - sizeof(PoolNode);

        sl::ScopedLock scopeLock(listLock);
        PoolRegion* scan = head;
        while (scan != nullptr && scan->base < region->base)
            scan = scan->next;
        
        if (scan == nullptr)
        {
            region->prev = tail;
            region->next = nullptr;
            if (tail != nullptr)
                tail->next = region;
            else
                head = region;
            tail = region;
        }
        else
        {
            region->next = scan;
            region->prev = scan->prev;
            if (region->prev != nullptr)
                region->prev->next = region;
            else
                head = region;
            scan->prev = region;
        }
    }

    void PoolAlloc::Init(size_t minAllocBytes)
    {
        sl::ScopedLock scopeLock(listLock);
        head = tail = nullptr;
        minAllocSize = minAllocBytes;

        Log("Kpool initialized: minSize=0x%lx", LogLevel::Verbose, minAllocSize);
    }

    void* PoolAlloc::Alloc(size_t bytes)
    {
        PoolRegion* region = head;
        PoolNode* scan = nullptr;
        while (region != nullptr)
        {
            region->lock.Lock();
            
            scan = region->first;
            while (scan != nullptr)
            {
                if (scan->length < bytes)
                {
                    scan = scan->next;
                    continue;
                }

                Split(scan, bytes);
                break;
            }

            if (scan != nullptr)
                break;
            
            PoolRegion* prevRegion = region;
            region = region->next;
            prevRegion->lock.Unlock();
        }

        if (scan == nullptr)
        {
            Expand(bytes, true); //true here is the lock's initial state.
            region = tail;
            Split(region->first, bytes);
            scan = region->first;
        }

        if (scan->prev != nullptr)
            scan->prev->next = scan->next;
        else
            region->first = scan->next;
        
        if (scan->next != nullptr)
            scan->next->prev = scan->prev;
        else
            region->last = scan->prev;
        region->lock.Unlock();

        sl::memset(scan->Data(), 0, scan->length);
        return scan->Data();
    }

    bool PoolAlloc::Free(void* ptr)
    {
        PoolNode* node = static_cast<PoolNode*>(ptr) - 1;
        for (PoolRegion* region = head; region != nullptr; region = region->next)
        {
            sl::ScopedLock scopeLock(region->lock);

            if (region->base > (uintptr_t)node)
                return false; //we've gone too far in the list.
            if ((uintptr_t)node < region->base)
                continue;
            
            if ((uintptr_t)node < (uintptr_t)region->first)
            {
                //prepend
                region->first->prev = node;
                node->next = region->first;
                node->prev = nullptr;
                region->first = node;
                MergeNext(node);
                return true;
            }

            PoolNode* scan = region->first;
            while (scan != nullptr && (uintptr_t)node < (uintptr_t)scan)
                scan = scan->next;
            
            if (scan == nullptr)
            {
                //append
                node->next = nullptr;
                node->prev = region->last;
                region->last->next = node;
                region->last = node;
                MergePrev(node);
                return true;
            }
            else
            {
                //insert
                node->next = scan->next;
                node->prev = scan;
                if (scan->next != nullptr)
                    scan->next->prev = node;
                scan->next = node;
                MergeNext(node);
                MergePrev(node);
                return true;
            }
        }

        return false;
    }
}
