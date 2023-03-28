#pragma once

#include <stddef.h>
#include <stdint.h>
#include <memory/Slab.h>
#include <memory/Pool.h>
#include <debug/Log.h>
#include <arch/Platform.h>

namespace Npk::Memory
{
    constexpr size_t SlabCount = 5;
    constexpr size_t SlabBaseSize = 32;

    class VirtualMemoryManager;
    
    class Heap
    {
    friend VirtualMemoryManager;
    private:
        SlabAlloc slabs[SlabCount];        
        PoolAlloc pool;

        void Init();

    public:
        static Heap& Global();

        Heap() = default;
        Heap(const Heap&) = delete;
        Heap& operator=(const Heap&) = delete;
        Heap(Heap&&) = delete;
        Heap& operator=(Heap&&) = delete;

        void* Alloc(size_t);
        void Free(void* ptr);
    };

    /*
        This allocator is inspired by the use of a 'magazine' in vmem allocators.
        This version acts a cache and can only allocate a single size (hence the use
        of the term slab), it also takes a function to check if heap access is available
        or not. The intended use to prevent heap alloc and free operations inside of
        interrupt handlers, which allowing some dynamically allocating structures to
        continue to grow, at least temporarily.
        TODO: abstract the allowMalloc condition to be a templateFunction, not hardcoded.
    */
    template<size_t CacheDepth>
    struct CachingSlab
    {
    private:
        struct OverdueItem
        {
            OverdueItem* next;
        };

        void* cache[CacheDepth];
        OverdueItem* overdueList;
        size_t cachedItems = 0;

    public:
        [[nodiscard]]
        inline void* Allocate(size_t length)
        {
            if (cachedItems > 0)
                return cache[--cachedItems];
            
            //check if we can refill cache from free list
            while (overdueList != nullptr)
            {
                void* item = overdueList;
                overdueList = overdueList->next;
                cache[cachedItems++] = item;
                
                if (cachedItems > 0)
                    return cache[--cachedItems];
            }

            //we need to refill the cache
            ASSERT(CoreLocal().runLevel != RunLevel::IntHandler, "Empty allocator cache but malloc not allowed.")
            for (; cachedItems < CacheDepth; cachedItems++)
                cache[cachedItems] = Heap::Global().Alloc(length);
            return Heap::Global().Alloc(length);
        }

        inline void Deallocate(void* ptr, size_t length)
        {
            (void)length; 
            //place it back in the cache
            if (cachedItems < CacheDepth)
            {
                cache[cachedItems++] = ptr;
                return;
            }

            //cache is full, see if we can immediately free (and free the overflow list too)
            if (CoreLocal().runLevel != RunLevel::IntHandler)
            {
                Heap::Global().Free(ptr);
                while (overdueList != nullptr)
                {
                    OverdueItem* item = overdueList;
                    overdueList = item->next;
                    Heap::Global().Free(item);
                }
            }

            //cant free right now, use the overflow list
            OverdueItem* item = new(ptr) OverdueItem();
            item->next = overdueList;
            overdueList = item;
        }
    };
}
