#pragma once

namespace sl
{
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

            const unsigned count = --(ptr->references);
            if (count > 0)
                return;

            if (Dtor != nullptr)
                Dtor(ptr);
            else
                delete ptr;
        }

        Handle(const Handle& other)
        {
            ptr = other.ptr;
            if (ptr != nullptr)
                ptr->references++;
        }

        Handle& operator=(const Handle& other)
        {
            if (ptr != nullptr)
                ptr->references--;

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
            if (ptr != nullptr)
                ptr->references--;
            
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
