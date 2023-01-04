#include <memory/Heap.h>
#include <debug/Log.h>
#include <arch/Platform.h>
#include <memory/Vmm.h>

namespace Npk::Memory
{
    void Heap::Init()
    {
        static_assert(PinnedSlabCount <= SlabCount, "Bad slab config values.");
        nextSlabBase = hhdmBase + hhdmLength + PageSize; //guard page
        size_t nextSlabSize = SlabBaseSize;
        size_t nextSlabCount = 1024;

        for (size_t i = 0; i < PinnedSlabCount; i++)
        {
            pinnedSlabs[i].Init(nextSlabBase, nextSlabSize, nextSlabCount);

            nextSlabBase += nextSlabSize * nextSlabCount;
            nextSlabSize *= 2;
            nextSlabCount /= 2;

            if (nextSlabCount == 0)
            {
                Log("Bad init values for kernel slab, resulted in trying to create slab with 0 entries.", LogLevel::Error);
                break;
            }
        }

        nextSlabSize = SlabBaseSize;
        nextSlabCount = 1024;
        for (size_t i = 0; i < SlabCount; i++)
        {
            slabs[i].Init(0, nextSlabSize, nextSlabCount);

            nextSlabSize *= 2;
            nextSlabCount /= 2;
            if (nextSlabCount == 0)
            {
                Log("Bad init values for kernel slab, resulted in trying to create slab with 0 entries.", LogLevel::Error);
                break;
            }
        }

        pinnedPool.Init(pinnedSlabs[PinnedSlabCount - 1].Size(), true);
        pool.Init(slabs[SlabCount - 1].Size(), false);
    }

    Heap globalHeap;
    Heap& Heap::Global()
    { return globalHeap; }

    void* Heap::Alloc(size_t size, bool pinned)
    {
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
        
        PoolAlloc* poolAlloc = pinned ? &pinnedPool : &pool;
        SlabAlloc* slabAllocs = pinned ? pinnedSlabs : slabs;
        if (slabIndex < (pinned ? PinnedSlabCount : SlabCount))
            return slabAllocs[slabIndex].Alloc(nextSlabBase);
        return poolAlloc->Alloc(size);
    }

    void Heap::Free(void* ptr, bool pinned)
    {
        if (ptr == nullptr)
            return;
        
        PoolAlloc* poolAlloc = pinned ? &pinnedPool : &pool;
        SlabAlloc* slabAllocs = pinned ? pinnedSlabs : slabs;

        for (size_t i = 0; i < (pinned ? PinnedSlabCount : SlabCount); i++)
        {
            if (slabAllocs[i].Free(ptr))
                return;
        }
        if (!poolAlloc->Free(ptr))
            Log("Kernel heap %sfailed to free at 0x%lx ", LogLevel::Error, pinned ? "(pinned) " : "", (uintptr_t)ptr);
    }
}
