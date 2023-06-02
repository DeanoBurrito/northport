#include <memory/Heap.h>
#include <debug/Log.h>
#include <arch/Platform.h>

namespace Npk::Memory
{
    void Heap::Init()
    {
        size_t nextSlabSize = SlabBaseSize;
        size_t nextSlabCount = 2 * PageSize;
        for (size_t i = 0; i < SlabCount; i++)
        {
            slabs[i].Init(nextSlabSize, nextSlabCount, 0);
            nextSlabSize *= 2;
        }

        pool.Init(slabs[SlabCount - 1].Size());
    }

    void* Heap::DoSlabAlloc(size_t index)
    {
        ASSERT(index < SlabCount, "Bad slab index");
        
        if (!CoreLocalAvailable() || CoreLocal()[LocalPtr::HeapCache] == nullptr)
            return slabs[index].Alloc();

        HeapCacheData* localState = static_cast<HeapCacheData*>(CoreLocal()[LocalPtr::HeapCache]);
        auto& cache = localState->magazines[index];

    try_alloc:
        //TODO: locking around cache state (prevent self-deadlocks)
        if (cache.loaded->count > 0)
            return cache.loaded->ptrs[--cache.loaded->count];
        if (cache.prev->count != 0)
        {
            sl::Swap(cache.loaded, cache.prev);
            goto try_alloc;
        }
        //TODO: exchange previous mag with depo mag, goto try_alloc

        return slabs[index].Alloc();
    }

    void Heap::DoSlabFree(size_t index, void* ptr)
    {
        ASSERT(index < SlabCount, "Bad slab index");

        if (!CoreLocalAvailable() || CoreLocal()[LocalPtr::HeapCache] == nullptr)
        {
            if (!slabs[index].Free(ptr))
                Log("KSlab (%lub) failed to free at 0x%lx", LogLevel::Error, slabs[index].Size(), (uintptr_t)ptr);
            return;
        }
        HeapCacheData* localState = static_cast<HeapCacheData*>(CoreLocal()[LocalPtr::HeapCache]);
        auto& cache = localState->magazines[index];

    try_free:
        //TODO: locking around cache state (prevent self-deadlocks)
        if (cache.loaded->count != SlabCacheSize - 1)
            cache.loaded->ptrs[cache.loaded->count++] = ptr;
        if (cache.prev->count == 0)
        {
            sl::Swap(cache.loaded, cache.prev);
            goto try_free;
        }
        //TODO: get empty mag from depo
        //TODO: try allocate empty mag if all else fails

        if (!slabs[index].Free(ptr))
            Log("KSlab (%lub) failed to free at 0x%lx", LogLevel::Error, slabs[index].Size(), (uintptr_t)ptr);
    }

    Heap globalHeap;
    Heap& Heap::Global()
    { return globalHeap; }
    
    void Heap::CreateCaches()
    {
        ASSERT(CoreLocalAvailable(), "Core-local store not available");
        ASSERT(CoreLocal()[LocalPtr::HeapCache] == nullptr, "Core-local heap cache double init?")

        //TODO: expose tunable variables for cache sizes + depth, and a global enable/disable

    }

    void* Heap::Alloc(size_t size)
    {
        //TODO: tracable allocations

        if (CoreLocalAvailable()) //TODO: what about DPCs? would be nice to allow dynamic memory access there.
            ASSERT(CoreLocal().runLevel == RunLevel::Normal, "Heap allocations only allowed at normal run level.");

        if (size == 0)
            return nullptr;
        
        size_t counter = SlabBaseSize;
        size_t slabIndex = 0;
        while (counter < size)
        {
            counter *= 2;
            slabIndex++;
        }
        
        if (slabIndex < SlabCount)
            return DoSlabAlloc(slabIndex);
        return pool.Alloc(size);
    }

    void Heap::Free(void* ptr, size_t length)
    {
        if (ptr == nullptr)
            return;
        
        size_t counter = SlabBaseSize;
        size_t slabIndex = 0;
        while (counter < length)
        {
            counter *= 2;
            slabIndex++;
        }
        if (slabIndex < SlabCount)
        {
            DoSlabFree(slabIndex, ptr);
            return;
        }

        if (!pool.Free(ptr))
            Log("KPool failed to free %lub at 0x%lx", LogLevel::Error, length, (uintptr_t)ptr);
    }
}
