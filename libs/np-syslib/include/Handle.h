#pragma once

#include <stddef.h>
#include <Atomic.h>

namespace sl
{
    using RefCount = Atomic<size_t>;
    using RefCountSmall = Atomic<short>;

    template<typename T>
    inline void NoHandleDtor(T*)
    {}

    template<typename T, void (*Dtor)(T*) = nullptr>
    class Handle
    {
    private:
        T* ptr;

    public:
        Handle() : ptr(nullptr)
        {}

        Handle(T* p) : ptr(p)
        {
            if (ptr != nullptr)
                ptr->references++;
        }

        ~Handle()
        {
            if (ptr == nullptr)
                return;

            Release();
        }

        Handle(const Handle& other)
        {
            ptr = other.ptr;
            if (ptr != nullptr)
                ptr->references++;
        }

        Handle& operator=(const Handle& other)
        {
            if (other.ptr == ptr)
                return *this;
            if (ptr != nullptr)
                Release();

            ptr = other.ptr;
            if (ptr != nullptr)
                ptr->references++;
            return *this;
        }

        Handle(Handle&& from)
        {
            ptr = from.ptr;
            from.ptr = nullptr;
        }

        Handle& operator=(Handle&& from)
        {
            if (from.ptr == ptr)
                return *this;
            if (ptr != nullptr)
                Release();
            
            ptr = from.ptr;
            from.ptr = nullptr;
            return *this;
        }

        void Release()
        {
            const unsigned count = --(ptr->references);
            if (count > 0)
                return;

            if (Dtor != nullptr)
                Dtor(ptr);
            else
                delete ptr;
            ptr = nullptr;
        }

        constexpr operator bool() const
        { return ptr != nullptr; }

        constexpr bool Valid() const
        { return ptr != nullptr; }

        constexpr const T* operator*() const
        { return ptr; }

        constexpr T* operator*()
        { return ptr; }

        constexpr const T* operator->() const
        { return ptr; }

        constexpr T* operator->()
        { return ptr; }
    };
}
