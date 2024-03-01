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
        constexpr IntPtr(const void* p) = delete;
        constexpr IntPtr(decltype(nullptr)) : ptr(nullptr) {}

        template <typename AsType>
        constexpr AsType* As() const
        {
            return reinterpret_cast<AsType*>(ptr);
        }

        template <typename AsType>
        constexpr AsType* As(const size_t byteOffset) const
        {
            return reinterpret_cast<AsType*>((void*)(raw + byteOffset));
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

    template <typename BackingType>
    union ConstIntPtr
    {
        BackingType raw;
        const void* ptr;

        constexpr ConstIntPtr() : raw(0) {}
        constexpr ConstIntPtr(BackingType r) : raw(r) {}
        constexpr ConstIntPtr(const void* p) : ptr(p) {}
        constexpr ConstIntPtr(decltype(nullptr)) : ptr(nullptr) {}

        template <typename AsType>
        constexpr const AsType* As() const
        {
            return reinterpret_cast<const AsType*>(ptr);
        }

        template <typename AsType>
        constexpr const AsType* As(const size_t byteOffset) const
        {
            return reinterpret_cast<const AsType*>((void*)(raw + byteOffset));
        }

        constexpr ConstIntPtr<BackingType> Offset(size_t offset) const
        { return ConstIntPtr(raw + offset); }

        template<typename Word>
        Word Read() const
        {
            return *reinterpret_cast<const volatile Word*>(ptr);
        }
    };

    using NativePtr = IntPtr<uintptr_t>;
    using CNativePtr = ConstIntPtr<uintptr_t>;
}
