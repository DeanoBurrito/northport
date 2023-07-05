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

        sl::ScopedLock scopeLock(cache.lock); //TODO: potential for self deadlock here?
    try_alloc:
        if (cache.loaded->count > 0)
            return cache.loaded->ptrs[--(cache.loaded->count)];
        if (cache.prev->count != 0)
        {
            sl::Swap(cache.loaded, cache.prev);
            goto try_alloc;
        }
        
        auto& depot = cacheDepot[index];
        depot.lock.Lock();
        SlabCache* mag = depot.fullCaches.PopFront();
        if (mag != nullptr)
        {
            depot.fullCaches.PushFront(cache.prev);
            depot.lock.Unlock();

            cache.prev = cache.loaded;
            cache.loaded = mag;
            return cache.loaded->ptrs[--(cache.loaded->count)];
        }
        depot.lock.Unlock();
        scopeLock.Release();

        //all else failed, call the global slab allocator directly.
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

        sl::ScopedLock scopeLock(cache.lock);
    try_free:
        if (cache.loaded->count < SlabCacheSize - 1)
        {
            cache.loaded->ptrs[(cache.loaded->count)++] = ptr;
            return;
        }
        if (cache.prev->count == 0)
        {
            sl::Swap(cache.loaded, cache.prev);
            goto try_free;
        }

        auto& depot = cacheDepot[index];
        depot.lock.Lock();
        SlabCache* mag = depot.freeCaches.PopFront();
        if (mag != nullptr)
        {
            depot.freeCaches.PushFront(cache.prev);
            depot.lock.Unlock();

            cache.prev = cache.loaded;
            cache.loaded = mag;
            cache.loaded->ptrs[(cache.loaded->count)++] = ptr;
            return;
        }
        depot.lock.Unlock();
        scopeLock.Release();

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
        HeapCacheData* cache = new HeapCacheData();
        for (size_t i = 0; i < SlabCount; i++)
        {
            cache->magazines[i].prev = new SlabCache();
            cache->magazines[i].loaded = new SlabCache();
            cache->magazines[i].loaded->count = SlabCacheSize;
            cache->magazines[i].prev->count = 0;
            
            //prefill the loaded magazine
            for (size_t j = 0; j < SlabCacheSize; j++)
                cache->magazines[i].loaded->ptrs[j] = slabs[i].Alloc();
        }
        
        CoreLocal()[LocalPtr::HeapCache] = cache;
        Log("Attached core-local allocator caches, depth=%lu", LogLevel::Verbose, SlabCacheSize);
    }

    void* Heap::Alloc(size_t size)
    {
        //TODO: tracable allocations
        //if (CoreLocalAvailable() && CoreLocal().runLevel != RunLevel::Normal)
        //    Log("Heap access at elevated run level.", LogLevel::Warning);

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
        //if (CoreLocalAvailable() && CoreLocal().runLevel != RunLevel::Normal)
        //    Log("Heap access at elevated run level.", LogLevel::Warning);

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
            return DoSlabFree(slabIndex, ptr);

        if (!pool.Free(ptr))
            Log("KPool failed to free %lub at 0x%lx", LogLevel::Error, length, (uintptr_t)ptr);
    }
}
