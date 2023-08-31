#pragma once

#include <stddef.h>
#include <stdint.h>
#include <memory/Slab.h>
#include <memory/Pool.h>
#include <debug/Log.h>
#include <containers/List.h>

namespace Npk::Memory
{
    constexpr size_t SlabCount = 5;
    constexpr size_t SlabBaseSize = 32;
    //NOTE: this is tuned so that sizeof(SlabCache) fits within a slab
    //with zero wasted space.
    constexpr size_t SlabCacheSize = 14;

    struct SlabCache
    {
        size_t count;
        SlabCache* next;
        void* ptrs[SlabCacheSize];

        constexpr SlabCache() : count(0), next(nullptr), ptrs{}
        {}
    };

    struct HeapCacheData
    {
        struct
        {
            sl::SpinLock lock;
            SlabCache* loaded;
            SlabCache* prev;
        } magazines[SlabCount];
    };

    void CreateLocalHeapCaches();

    class Heap
    {
    private:
        SlabAlloc slabs[SlabCount];
        struct
        {
            sl::IntrFwdList<SlabCache> fullCaches;
            sl::IntrFwdList<SlabCache> freeCaches;
            sl::SpinLock lock;
        } cacheDepot[SlabCount];
        PoolAlloc pool;

        void* DoSlabAlloc(size_t index);
        void DoSlabFree(size_t index, void* ptr);

    public:
        static Heap& Global();
        void Init();

        Heap() = default;
        Heap(const Heap&) = delete;
        Heap& operator=(const Heap&) = delete;
        Heap(Heap&&) = delete;
        Heap& operator=(Heap&&) = delete;

        bool SwapCache(SlabCache** ptr, size_t i);

        [[nodiscard]]
        void* Alloc(size_t size);
        void Free(void* ptr, size_t length);
    };
}
