#pragma once

#include <Types.hpp>
#include <Atomic.hpp>

namespace sl
{
    using RefCount = Atomic<size_t>;

    template<typename T, RefCount T::*refs>
    bool IncrementRefCount(T* ptr)
    {
        if (ptr == nullptr)
            return false;

        while (true)
        {
            auto expected = (ptr->*refs).Load(sl::Acquire);
            if (expected == 0)
                return false;

            const auto desired = expected + 1;
            if ((ptr->*refs).CompareExchange(expected, desired, sl::AcqRel))
                return true;
        }

        return false;
    }

    template<typename T, RefCount T::*refs>
    bool DecrementRefCount(T* ptr)
    {
        if (ptr == nullptr)
            return false;

        const bool last = (ptr->*refs).FetchSub(1) == 1;

        return last;
    }

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
            if (!Acquire())
                ptr = nullptr;
        }

        ~Ref()
        {
            Release();
        }

        Ref(const Ref& other)
        {
            ptr = other.ptr;
            if (!Acquire())
                ptr = nullptr;
        }

        Ref& operator=(const Ref& other)
        {
            if (ptr != nullptr)
                Release();

            ptr = other.ptr;
            if (!Acquire())
                ptr = nullptr;

            return *this;
        }

        Ref(Ref&& from)
        {
            ptr = from.ptr;
            from.ptr = nullptr;
        }

        Ref& operator=(Ref&& from)
        {
            Release();

            ptr = from.ptr;
            from.ptr = nullptr;
            return *this;
        }

        bool Acquire()
        {
            return IncrementRefCount<T, refs>(ptr);
        }

        void Release()
        {
            if (!DecrementRefCount<T, refs>(ptr))
                return;

            if (WhenZero != nullptr)
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
