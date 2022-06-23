#include <memory/KernelHeap.h>
#include <Log.h>

/*
    If you've come looking for how the kernel heap works, here's the explanation:
    When an alloc request is made a series of slab allocators is checked. The first slab
    that can contained the request is used to allocate the memory. If the reuqest is too
    large, a pool allocator (linked list of memory regions) is used.
    To free, similar logic is employed. Each slab is asked to free the pointer, with the
    free request cascading to the larger slabs if the smaller ones don't own the memory.
    Again, if the slabs can complete the request, it's assumed to be the pool's job.
*/

namespace Kernel::Memory
{
    KernelHeap globalKernelHeap;
    KernelHeap* KernelHeap::Global()
    { return &globalKernelHeap; }

    void KernelHeap::Init(sl::NativePtr base, bool enableDebuggingFeatures)
    {
        const sl::NativePtr poolBase = base.raw + KernelHeapPoolOffset;
        size_t nextBlockSize = KernelSlabBaseSize;
        size_t nextBlockCount = 2048;
        
        for (size_t i = 0; i < KernelSlabCount; i++)
        {
            //blocks for per slab: 32 bytes = 2048, 64/128 bytes = 1024, 256 bytes = 512, 512 bytes = 256.
            switch (i)
            {
            case 1: nextBlockCount = 1024; break;
            case 3: nextBlockCount = 512; break;
            case 4: nextBlockCount = 256; break;
            }

            //one of the following init methods should be used. Leaving the last argument blank means regular mode,
            //setting it a virtual address means the page-heap allocator will start at that address. Leave last arg null by default.
            if (enableDebuggingFeatures)
                slabs[i].Init(base, nextBlockSize, nextBlockCount, (base.raw + 8 * GB) + i * GB);
            else
                slabs[i].Init(base, nextBlockSize, nextBlockCount);

            base.raw = slabs[i].Region().base.raw + slabs[i].Region().length;
            nextBlockSize *= 2;
        }

        pool.Init(poolBase);
        if (enableDebuggingFeatures)
            Log("Kernel heap running with debug helpers.", LogSeverity::Warning);
        Log("Kernel heap initialized.", LogSeverity::Info);
    }

    void* KernelHeap::Alloc(size_t size)
    {
        //NOTE: we lock per-allocator, rather than at a global scale
        if (size == 0)
            return nullptr;

        for (size_t i = 0; i < KernelSlabCount; i++)
        {
            if (size <= slabs[i].SlabSize())
                return slabs[i].Alloc();
        }
        
        void* poolAddr = pool.Alloc(size);
        if (poolAddr == nullptr)
            Log("Kernel heap failed to allocate memory.", LogSeverity::Error);
        
        return poolAddr;
    }

    void KernelHeap::Free(void* ptr)
    {
        if (ptr == nullptr)
            return;

        for (size_t i = 0; i < KernelSlabCount; i++)
        {
            if (slabs[i].Free(ptr))
                return;
        }
        
        if (pool.Free(ptr))
            return;
        
        Logf("Kernel heap unable to free address 0x%lx, not inside any allocators.", LogSeverity::Error, (uint64_t)ptr);
    }

    void KernelHeap::GetStats(HeapMemoryStats& stats) const
    {
        for (size_t i = 0; i < KernelSlabCount; i++)
            slabs[i].GetStats(stats.slabStats[i]);
        pool.GetStats(stats.poolStats);
        
        stats.slabsGlobalBase = slabs[0].Region().base;
        stats.slabCount = KernelSlabCount;
    }
}

void* malloc(size_t size)
{ return Kernel::Memory::KernelHeap::Global()->Alloc(size); }

void free(void* ptr)
{ Kernel::Memory::KernelHeap::Global()->Free(ptr); }
