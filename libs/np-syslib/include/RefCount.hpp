#pragma once

#include <Types.hpp>
#include <Atomic.hpp>

namespace sl
{
    using RefCount = Atomic<size_t>;

    template<typename T, RefCount T::*refs, void (*WhenZero)(T*) = nullptr>
    class Ref
    {
    private:
        T* ptr;

    public:
        constexpr Ref() : ptr(nullptr)
        {}

        Ref(T* p) : ptr(p)
        {
            if (ptr != nullptr)
                (ptr->*refs)++;
        }

        ~Ref()
        {
            Release();
        }

        Ref(const Ref& other)
        {
            ptr = other.ptr;
            if (ptr != nullptr)
                (ptr->*refs)++;
        }

        Ref& operator=(const Ref& other)
        {
            if (ptr != nullptr)
                Release();

            ptr = other.ptr;
            if (ptr != nullptr)
                (ptr->*refs)++;
            return *this;
        }

        Ref(Ref&& from)
        {
            ptr = from.ptr;
            from.ptr = nullptr;
        }

        Ref& operator=(Ref&& from)
        {
            if (ptr != nullptr)
                (ptr->*refs)--;

            ptr = from.ptr;
            from.ptr = nullptr;
            return *this;
        }

        void Release()
        {
            if (ptr == nullptr)
                return;

            const bool zeroRefs = --(ptr->*refs) == 0;
            if (zeroRefs && WhenZero != nullptr)
                WhenZero(ptr);
            ptr = nullptr;
        }

        constexpr bool Valid() const
        { return ptr != nullptr; }

        constexpr const T& operator*() const
        { return *ptr; }

        constexpr T& operator*()
        { return *ptr; }

        constexpr const T* operator->() const
        { return ptr; }

        constexpr T* operator->()
        { return ptr; }
    };
}
