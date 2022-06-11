#include <heap/UserHeap.h>
#include <Logging.h>

namespace np::Userland
{
    UserHeap globalUserHeap;
    UserHeap* UserHeap::Global()
    { return &globalUserHeap; }

    void UserHeap::Init(sl::NativePtr base, bool enableDebuggingFeatures)
    {
        const sl::NativePtr poolBase = base.raw + UserPoolOffset;
        size_t nextBlockSize = UserSlabBaseSize;
        size_t nextBlockCount = 2048;
        
        for (size_t i = 0; i < UserSlabCount; i++)
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
            Log("User heap running with debug helpers.", LogLevel::Debug);
    }

    void* UserHeap::Alloc(size_t size)
    {
        //NOTE: we lock per-allocator, rather than at a global scale
        if (size == 0)
            return nullptr;

        for (size_t i = 0; i < UserSlabCount; i++)
        {
            if (size <= slabs[i].SlabSize())
                return slabs[i].Alloc();
        }
        
        void* poolAddr = pool.Alloc(size);
        if (poolAddr == nullptr)
            Log("User heap failed to allocate memory.", LogLevel::Error);
        
        return poolAddr;
    }

    void UserHeap::Free(void* ptr)
    {
        if (ptr == nullptr)
            return;

        for (size_t i = 0; i < UserSlabCount; i++)
        {
            if (slabs[i].Free(ptr))
                return;
        }
        
        if (pool.Free(ptr))
            return;
        
        Log("Unable to free user heap memory, address outside of all allocators.", LogLevel::Error);
    }
}

void* malloc(size_t size)
{ return np::Userland::UserHeap::Global()->Alloc(size); }

void free(void* ptr)
{ np::Userland::UserHeap::Global()->Free(ptr); }
