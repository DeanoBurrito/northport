#include <memory/Heap.h>
#include <debug/Log.h>
#include <arch/Platform.h>
#include <CppUtils.h>

namespace Npk::Memory
{
    void CreateLocalHeapCaches()
    {
        ASSERT(CoreLocalAvailable(), "CLS not available");
        ASSERT(CoreLocal()[LocalPtr::HeapCache] == nullptr, "Slab caches already exist for this core");

        HeapCacheData* cache = new HeapCacheData();
        for (size_t i = 0; i < SlabCount; i++)
        {
            cache->magazines[i].prev = new SlabCache();
            cache->magazines[i].prev->count = 0;
            cache->magazines[i].loaded = nullptr;
            Heap::Global().SwapCache(&cache->magazines[i].loaded, i);
        }

        CoreLocal()[LocalPtr::HeapCache] = cache;
        Log("Created core-local slab caches, depth=%lu", LogLevel::Info, SlabCacheSize);
    }

    void* Heap::DoSlabAlloc(size_t index)
    {
        ASSERT(index < SlabCount, "Bad slab index");
        return slabs[index].Alloc();

        /* The bulk of this function is to make use of per-core caches of pre-allocated
         * slabs, however if core local stuff isn't initialized yet or the cache isnt
         * available we allocate normally. This also makes it easy to disable the caches
         * if memory is scarce.
         */
        if (!CoreLocalAvailable() || CoreLocal()[LocalPtr::HeapCache] == nullptr)
            return slabs[index].Alloc();

        auto& cache = static_cast<HeapCacheData*>(CoreLocal()[LocalPtr::HeapCache])->magazines[index];
        sl::ScopedLock cacheLock(cache.lock); //TODO: deadlock potential?

        /* This algorithm is lifted out of the 'vmem and magazines' paper by Jeff Bonwick,
         * and if you're familiar with how that system works you've understood this one.
         */
        if (cache.loaded->count > 0)
            return cache.loaded->ptrs[--cache.loaded->count];
        if (cache.prev->count > 0)
        {
            sl::Swap(cache.loaded, cache.prev);
            return cache.loaded->ptrs[--cache.loaded->count];
        }

        if (SwapCache(&cache.loaded, index))
            return cache.loaded->ptrs[--cache.loaded->count];
        return slabs[index].Alloc();
    }

    void Heap::DoSlabFree(size_t index, void* ptr)
    {
        ASSERT(index < SlabCount, "Bad slab index");
        ASSERT(slabs[index].Free(ptr), "bad slab free");
        return;

        if (!CoreLocalAvailable() || CoreLocal()[LocalPtr::HeapCache] == nullptr)
        {
            if (!slabs[index].Free(ptr))
                Log("KSlab (%lu) failed to free at 0x%lx", LogLevel::Error, slabs[index].Size(), (size_t)ptr);
            return;
        }


        auto& cache = static_cast<HeapCacheData*>(CoreLocal()[LocalPtr::HeapCache])->magazines[index];
        sl::ScopedLock cacheLock(cache.lock);

        if (cache.loaded->count < SlabCacheSize - 1)
        {
            cache.loaded->ptrs[cache.loaded->count++] = ptr;
            return;
        }
        if (cache.prev->count < SlabCacheSize - 1)
        {
            sl::Swap(cache.loaded, cache.prev);
            cache.loaded->ptrs[cache.loaded->count++] = ptr;
            return;
        }
        if (SwapCache(&cache.loaded, index))
        {
            cache.loaded->ptrs[cache.loaded->count++] = ptr;
            return;
        }
        
        if (!slabs[index].Free(ptr))
            Log("KSlab (%lu) failed to free at 0x%lx", LogLevel::Error, slabs[index].Size(), (size_t)ptr);
    }

    Heap globalHeap;
    Heap& Heap::Global()
    { return globalHeap; }
    
    void Heap::Init()
    {
        size_t nextSlabSize = SlabBaseSize;
        size_t nextSlabCount = 64;
        for (size_t i = 0; i < SlabCount; i++)
        {
            slabs[i].Init(nextSlabSize, nextSlabCount, false);
            nextSlabSize *= 2;
        }

        pool.Init(slabs[SlabCount - 1].Size(), false);
    }

    bool Heap::SwapCache(SlabCache** ptr, size_t index)
    {
        if (ptr == nullptr || index >= SlabCount)
            return false;
        
        SlabCache* exchange = nullptr;
        auto& depot = cacheDepot[index];

        if (*ptr == nullptr || (*ptr)->count == 0)
        {
            depot.lock.Lock(); //TODO: potential lockfree operations here?
            if (*ptr != nullptr)
                depot.freeCaches.PushFront(*ptr);
            exchange = depot.fullCaches.PopFront();
            depot.lock.Unlock();

            if (exchange == nullptr)
            {
                //no available full caches, make one
                exchange = new SlabCache();
                exchange->next = nullptr;
                exchange->count = SlabCacheSize;
                for (size_t i = 0; i < exchange->count; i++)
                    exchange->ptrs[i] = slabs[index].Alloc();
            }
        }
        else
        {
            ASSERT((*ptr)->count == SlabCacheSize, "SlabCache neither full or empty");
            
            depot.lock.Lock();
            depot.fullCaches.PushFront(*ptr);
            exchange = depot.freeCaches.PopFront();
            depot.lock.Lock();

            if (exchange == nullptr)
            {
                //no free caches vailable, make one
                exchange = new SlabCache();
                exchange->next = nullptr;
                exchange->count = 0;
            }
        }

        if (exchange == nullptr)
            return false;
        *ptr = exchange;
        return true;
    }

    void* Heap::Alloc(size_t size)
    {
        //TODO: tracable allocations
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
            return DoSlabFree(slabIndex, ptr);

        if (!pool.Free(ptr))
            Log("KPool failed to free %lub at 0x%lx", LogLevel::Error, length, (uintptr_t)ptr);
    }
}
