#include <heap/UserPool.h>
#include <heap/UserHeap.h>
#include <SyscallFunctions.h>
#include <PlacementNew.h>
#include <Logging.h>
#include <Memory.h>
#include <Locks.h>

namespace np::Userland
{
    void UserPool::TrySplit(UserPoolNode* node, size_t allocSize)
    {
        if (node == nullptr)
            return;
        if (node->length <= allocSize + sizeof(UserPoolNode))
            return;
        
        const size_t latestLength = node->length - sizeof(UserPoolNode) - allocSize;
        UserPoolNode* latest = new(sl::NativePtr(node->Data()).As<void>(allocSize)) UserPoolNode(node, node->next, latestLength);

        latest->isFree = true;
        if (node->next)
            node->next->prev = latest;
        node->length = allocSize;
        node->next = latest;

        if (tail == node)
            tail = latest;

        usedBytes += sizeof(UserPoolNode);
    }

    void UserPool::TryMerge(UserPoolNode* node)
    {
        if (node == nullptr)
            return;
        if (!node->isFree)
            return;
        
        //try merge forwards first
        if (node->next != nullptr && node->next->isFree)
        {
            UserPoolNode* next = node->next;

            node->length += next->length + sizeof(UserPoolNode);
            if (next->next != nullptr)
                next->next->prev = node;
            node->next = next->next;

            if (tail == next)
                tail = node;

            usedBytes -= sizeof(UserPoolNode);
        }
        
        //then merge backwards
        if (node->prev != nullptr && node->prev->isFree)
        {
            UserPoolNode* prev = node->prev;

            prev->length += node->length + sizeof(UserPoolNode);
            if (node->next != nullptr)
                node->next->prev = prev;
            prev->next = node->next;

            if (tail == node)
                tail = prev;

            usedBytes -= sizeof(UserPoolNode);
        }
    }

    void UserPool::Expand(size_t allocSize)
    {
        const size_t newPages = (allocSize - (tail->isFree ? tail->length : 0)) / UserHeapPageSize + 1;
        const size_t newBytes = newPages * UserHeapPageSize * UserPoolExpandFactor;

        if (newPages > 1)
            Log("User heap pool is expanding by %u pages.", LogLevel::Debug, newPages * UserPoolExpandFactor);

        Syscall::MapMemory(allocRegion.base.raw + allocRegion.length, 
            newPages * UserHeapPageSize * UserPoolExpandFactor, Syscall::MemoryMapFlags::Writable);
        allocRegion.length += newBytes;

        if (tail->isFree)
            tail->length += newBytes;
        else
        {
            //add a new node after the tail
            UserPoolNode* latest = new(sl::NativePtr(tail->Data()).As<void>(tail->length)) 
                UserPoolNode(tail, nullptr, newBytes - sizeof(UserPoolNode));
            tail->next = latest;
            latest->isFree = true;
            tail = latest;
        }
    }
    
    void UserPool::Init(sl::NativePtr base)
    {
        allocRegion = { base, UserHeapStartSize };
        usedBytes = 0;

        Syscall::MapMemory(base.raw, UserHeapStartSize, Syscall::MemoryMapFlags::Writable);
        tail = head = new (base.ptr) UserPoolNode(nullptr, nullptr, UserHeapStartSize - sizeof(UserPoolNode));
        head->isFree = true;

        sl::SpinlockRelease(&lock);
    }

    void* UserPool::Alloc(size_t size)
    { 
        sl::ScopedSpinlock scopeLock(&lock);
        
        UserPoolNode* returnNode = nullptr;
        for (UserPoolNode* scan = head; scan != nullptr; scan = scan->next)
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
        if (tail->prev->isFree && tail->prev->length >= size)
            returnNode = tail->prev;
        else if (tail->isFree && tail->length >= size)
            returnNode = tail;

        if (returnNode != nullptr)
            goto claim_node;
        return nullptr;

claim_node:
        usedBytes += returnNode->length;
        returnNode->isFree = false;
        sl::memset(returnNode->Data(), 0, returnNode->length);
        return returnNode->Data();
    }

    bool UserPool::Free(sl::NativePtr where)
    {
        if (where.raw < allocRegion.base.raw || where.raw > allocRegion.length + allocRegion.base.raw)
            return false;

        sl::ScopedSpinlock scopeLock(&lock);
        for (UserPoolNode* scan = head; scan != nullptr; scan = scan->next)
        {
            if (scan->Data() != where.ptr)
                continue;
            
            //at this point scan contains our allocation
            if (scan->isFree)
            {
                np::Syscall::Log("Attempted to free upool memory already marked as free.", LogLevel::Error);
                return true;
            }

            usedBytes -= scan->length;
            scan->isFree = true;
            TryMerge(scan);
            return true;
        }

        return false; 
    }
}
