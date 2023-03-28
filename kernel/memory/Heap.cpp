#include <memory/Heap.h>
#include <debug/Log.h>
#include <arch/Platform.h>
#include <memory/Vmm.h>

namespace Npk::Memory
{
    void Heap::Init()
    {
        size_t nextSlabSize = SlabBaseSize;
        size_t nextSlabCount = 1024;
        for (size_t i = 0; i < SlabCount; i++)
        {
            slabs[i].Init(nextSlabSize, nextSlabCount);

            nextSlabSize *= 2;
            nextSlabCount /= 2;
            if (nextSlabCount == 0)
            {
                Log("Bad init values for kernel slab, resulted in trying to create slab with 0 entries.", LogLevel::Error);
                break;
            }
        }

        pool.Init(slabs[SlabCount - 1].Size());
    }

    Heap globalHeap;
    Heap& Heap::Global()
    { return globalHeap; }

    void* Heap::Alloc(size_t size)
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
        
        if (slabIndex < SlabCount)
            slabs[slabIndex].Alloc();
        return pool.Alloc(size);
    }

    void Heap::Free(void* ptr)
    {
        if (ptr == nullptr)
            return;
        
        for (size_t i = 0; i < SlabCount; i++)
        {
            if (slabs[i].Free(ptr))
                return;
        }

        if (!pool.Free(ptr))
            Log("Kernel heap failed to free at 0x%lx ", LogLevel::Error, (uintptr_t)ptr);
    }
}
