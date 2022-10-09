#include <memory/Heap.h>
#include <debug/Log.h>
#include <arch/Platform.h>
#include <memory/Vmm.h>

namespace Npk::Memory
{
    void Heap::Init()
    {
        nextSlabBase = hhdmBase + HhdmLimit;
        size_t nextSlabSize = SlabBaseSize;
        size_t nextSlabCount = 1024;

        for (size_t i = 0; i < SlabCount; i++)
        {
            slabs[i].Init(nextSlabBase, nextSlabSize, nextSlabCount);

            nextSlabBase += nextSlabSize * nextSlabCount;
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
            return slabs[slabIndex].Alloc(nextSlabBase);

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
            Log("Kernel heap failed to free at 0x%lx", LogLevel::Debug, (uintptr_t)ptr);
    }
}
