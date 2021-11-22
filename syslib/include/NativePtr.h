#pragma once

#include <stddef.h>
#include <stdint.h>

#if __SIZEOF_SIZE_T__ == 8
    #define NativeUInt uint64_t
#elif  __SIZEOF_SIZE_T__ == 4
    #define NativeUInt uint32_t
#else
    #error "syslib/include/Memory.h cannot determine cpu native word size. Unable to define NativeUInt."
#endif

namespace sl
{
    template <typename BackingType>
    union IntPtr
    {
        BackingType raw;
        void* ptr;

        IntPtr() : raw(0) {}
        IntPtr(BackingType r) : raw(r) {}
        IntPtr(void* p) : ptr(p) {}

        //type safety is thrown to the wind here with reinterpret cast, but we've too cool for that.
        //NOTE: we are not.
        template <typename AsType>
        AsType* As()
        {
            return reinterpret_cast<AsType*>(ptr);
        }

        template <typename AsType>
        AsType* As(const size_t byteOffset)
        {
            return reinterpret_cast<AsType*>((void*)(raw + byteOffset));
        }

        template <typename AsType>
        AsType* As() const
        {
            return reinterpret_cast<const AsType*>(ptr);
        }

        template <typename AsType>
        AsType* As(const size_t byteOffset) const
        {
            return reinterpret_cast<const AsType*>((void*)(raw + byteOffset));
        }
    };

    typedef IntPtr<NativeUInt> NativePtr;
}
