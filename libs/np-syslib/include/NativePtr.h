#pragma once

#include <stdint.h>
#include <stddef.h>

namespace sl
{
    template <typename BackingType>
    union IntPtr
    {
        BackingType raw;
        void* ptr;

        constexpr IntPtr() : raw(0) {}
        constexpr IntPtr(BackingType r) : raw(r) {}
        constexpr IntPtr(void* p) : ptr(p) {}

        //type safety is thrown to the wind here with reinterpret cast, but we've too cool for that.
        //NOTE: we are not.
        template <typename AsType>
        constexpr AsType* As()
        {
            return reinterpret_cast<AsType*>(ptr);
        }

        template <typename AsType>
        constexpr AsType* As(const size_t byteOffset)
        {
            return reinterpret_cast<AsType*>((void*)(raw + byteOffset));
        }

        template <typename AsType>
        constexpr AsType* As() const
        {
            return reinterpret_cast<const AsType*>(ptr);
        }

        template <typename AsType>
        constexpr AsType* As(const size_t byteOffset) const
        {
            return reinterpret_cast<const AsType*>((void*)(raw + byteOffset));
        }

        constexpr IntPtr<BackingType> Offset(size_t offset) const
        { return IntPtr(raw + offset); }

        template<typename Word>
        void Write(Word value) const
        {
            *reinterpret_cast<volatile Word*>(ptr) = value;
        }

        template<typename Word>
        Word Read() const
        {
            return *reinterpret_cast<volatile Word*>(ptr);
        }

        template<typename Word, bool growDown = true>
        void Push(Word value)
        {
            if (growDown)
                raw -= sizeof(Word);
            *reinterpret_cast<volatile Word*>(ptr) = value;
            if (!growDown)
                raw += sizeof(Word);
        }

        template<typename Word, bool growDown = true>
        Word Pop()
        {
            if (growDown)
                raw -= sizeof(Word);
            Word value = *reinterpret_cast<volatile Word*>(ptr);
            if (!growDown)
                raw += sizeof(Word);
            return value;
        }
    };

    using NativePtr = IntPtr<uintptr_t>;
}
