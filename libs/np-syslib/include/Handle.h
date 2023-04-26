#pragma once

namespace sl
{
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
            if (ptr)
                ptr->references++;
        }

        ~Handle()
        {
            if (ptr)
            {
                const unsigned count = (ptr->references--);

                if (count == 0 && Dtor != nullptr)
                    Dtor(ptr);
            }
        }

        Handle(const Handle& other)
        {
            ptr = other.ptr;
            if (ptr)
                ptr->references++;
        }

        Handle& operator=(const Handle& other)
        {
            ptr = other.ptr;
            if (ptr)
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
            ptr = from.ptr;
            from.ptr = nullptr;
            return *this;
        }

        constexpr operator bool()
        { return ptr != nullptr; }

        constexpr bool Valid()
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
