#pragma once

#include <NativePtr.h>
#include <memory/KernelSlab.h>
#include <memory/KernelPool.h>
#include <Maths.h>

namespace Kernel::Memory
{
    //these values control how the initial heap is setup. They're suggestions - not limits.
    constexpr size_t KernelSlabCount = 5;
    constexpr size_t KernelSlabBaseSize = 32;
    constexpr size_t KernelHeapPoolOffset = 1 * GB;
    constexpr size_t KernelHeapStartSize = 32 * KB;
    constexpr size_t KernelPoolExpandFactor = 2;
    
    //this is a chonky boi of a struct.
    struct HeapMemoryStats
    {
        sl::NativePtr slabsGlobalBase;
        HeapPoolStats poolStats;
        size_t slabCount;
        HeapSlabStats slabStats[KernelSlabCount];
    };

    class KernelHeap
    {
    private:
        sl::NativePtr nextSlabBase;
        KernelSlab slabs[KernelSlabCount];
        KernelPool pool;

    public:
        static KernelHeap* Global();

        void Init(sl::NativePtr base, bool enableDebuggingFeatures);
        void* Alloc(size_t size);
        void Free(void* ptr);

        //HeapMemoryStats is massive, so let the caller allocate it (usually just on the stack).
        void GetStats(HeapMemoryStats& populateMe) const;
    };
}

void* malloc(size_t size);
void free(void* ptr);
