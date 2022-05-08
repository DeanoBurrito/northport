#include <GeneralHeap.h>
#include <SyscallFunctions.h>
#include <Userland.h>
#include <Locks.h>

void* malloc(size_t size)
{
    return np::Userland::GeneralHeap::Default().Alloc(size);
}

void free(void* ptr)
{ 
    np::Userland::GeneralHeap::Default().Free(ptr);
}

namespace np::Userland
{
    bool GeneralHeapNode::CombineWithNext(GeneralHeap& heap)
    {
        if (!free)
            return false;
        if (next == nullptr)
            return false;
        if (!next->free)
            return false;
        
        if (heap.tail == next)
            heap.tail = this;
        
        length += next->length + sizeof(GeneralHeapNode);
        next = next->next;
        if (next != nullptr)
            next->prev = this;
        
        return true;
    }

    void GeneralHeapNode::Split(size_t neededSize, GeneralHeap& heap)
    {
        const size_t actualSize = sizeof(GeneralHeapNode) + neededSize;
        if (actualSize > length)
            return;

        GeneralHeapNode* nextNode = sl::NativePtr(this).As<GeneralHeapNode>(actualSize);
        nextNode->free = this->free;
        nextNode->next = this->next;
        nextNode->prev = this;
        if (next != nullptr)
            next->prev = nextNode;
        next = nextNode;
        nextNode->length = length - actualSize;
        length = neededSize;

        if (heap.tail == this)
            heap.tail = nextNode;
    }

    void GeneralHeap::Expand(size_t requiredLength)
    {
        const size_t requiredBytes = ((sizeof(GeneralHeapNode) + requiredLength) / heapExpandRequestSize + 1) * heapExpandRequestSize;
        uintptr_t endOfHeapAddr = (uintptr_t)tail + tail->length + sizeof(GeneralHeapNode);

        auto result = Syscall::MapMemory(endOfHeapAddr, requiredBytes * heapExpandFactor, Syscall::MemoryMapFlags::Writable);
        if (result.base == 0)
        {
            Syscall::Log("Could not expand heap, mapping failed.", Syscall::LogLevel::Error);
            return;
        }
        else if (result.length == 0)
        {
            Syscall::Log("Attempted to expand user heap: expaned into already mapped region?", Syscall::LogLevel::Error);
            return;
        }

        tail->next = sl::NativePtr(endOfHeapAddr).As<GeneralHeapNode>();
        GeneralHeapNode* next = tail->next;
        
        next->next = nullptr;
        next->prev = tail;
        next->free = true;
        next->length = requiredBytes - sizeof(GeneralHeapNode);
        tail = next;
    }

    GeneralHeap defaultGeneralHeap;
    GeneralHeap& GeneralHeap::Default()
    { return defaultGeneralHeap; }

    void GeneralHeap::Init(sl::NativePtr startAddress, size_t length)
    {
        sl::ScopedSpinlock scopeLock(&lock);
        
        if (initialized)
            return;
        initialized = true;

        auto result = np::Syscall::MapMemory(startAddress.raw, length, Syscall::MemoryMapFlags::Writable);
        if (result.base == 0)
            return; //return base is NULL, mapping failed.
        
        startAddress = result.base;
        if (result.length > 0)
            length = result.length; //if zero was returned, the mapping already existed (concerning) so use the existing length

        tail = head = startAddress.As<GeneralHeapNode>();
        head->next = head->prev = nullptr;
        head->length = length - sizeof(GeneralHeapNode);
        head->free = true;
    }

    void* GeneralHeap::Alloc(size_t size)
    {
        if (size == 0)
            return nullptr;

        sl::ScopedSpinlock scopeLock(&lock);

        GeneralHeapNode* scan = head;
        while (scan != nullptr)
        {
            if (!scan->free)
            {
                scan = scan->next;
                continue;
            }

            if (scan->length == size)
            {
                scan->free = false;
                bytesUsed += size;
                return sl::NativePtr(scan).As<void>(sizeof(GeneralHeapNode));
            }

            if (scan->length > size)
            {
                if (scan->length - (size + sizeof(GeneralHeapNode)) > heapMinAllocSize)
                    scan->Split(size, *this);
                
                scan->free = false;
                bytesUsed += size + sizeof(GeneralHeapNode);
                return sl::NativePtr(scan).As<void>(sizeof(GeneralHeapNode));
            }

            scan = scan->next;
        }
        
        Expand(size);
        scan = tail;
        scan->Split(size, *this);
        scan->free = false;
        bytesUsed += size + sizeof(GeneralHeapNode);
        return sl::NativePtr(scan).As<void>(sizeof(GeneralHeapNode));
    }

    void GeneralHeap::Free(void* ptr)
    {
        if (ptr == nullptr)
            return;

        sl::ScopedSpinlock scopeLock(&lock);

        sl::NativePtr where(ptr);
        where.raw -= sizeof(GeneralHeapNode);
        if (where.raw < (uintptr_t)head || where.raw > (uintptr_t)tail)
            return;
        
        GeneralHeapNode* node = where.As<GeneralHeapNode>();
        node->free = true;
        bytesUsed -= node->length;

        if (node->CombineWithNext(*this))
            bytesUsed -= sizeof(GeneralHeapNode);
        if (node->prev != nullptr)
            node->prev->CombineWithNext(*this);
    }
}
