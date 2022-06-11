#pragma once

#include <Maths.h>
#include <heap/UserSlab.h>
#include <heap/UserPool.h>

namespace np::Userland
{
    constexpr size_t UserSlabCount = 5;
    constexpr size_t UserSlabBaseSize = 32;
    constexpr size_t UserPoolOffset = 1 * GB;
    constexpr size_t UserHeapStartSize = 32 * KB;
    constexpr size_t UserHeapPageSize = 0x1000; //this hint lets us make the most use of a mapped page.
    constexpr size_t UserPoolExpandFactor = 2;
    
    class UserHeap
    {
    private:
        sl::NativePtr nextSlabBase;
        UserSlab slabs[UserSlabCount];
        UserPool pool;

    public:
        static UserHeap* Global();

        //NOTE: enabling debugging features is significantly more expensive for a user app,
        //because every alloc must have a memory map call for page-heap style allocs.
        //Best left disabled unless *really* needed.
        void Init(sl::NativePtr base, bool enableDebuggingFeatures);
        void* Alloc(size_t size);
        void Free(void* ptr);
    };
}

void* malloc(size_t size);
void free(void* ptr);
