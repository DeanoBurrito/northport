#pragma once

#include <Utilities.h>
#include <PlacementNew.h>
#include <Memory.h>

namespace sl
{
    struct OptionalNoType
    {};

    constexpr static inline OptionalNoType OptNoType;
    
    template<typename T>
    class Optional
    {
    private:
        alignas(T) uint8_t store[sizeof(T)];
        bool isValid;

        const T* Get() const
        {
            return sl::Launder(reinterpret_cast<const T*>(store));
        }

        T* Get()
        {
            return sl::Launder(reinterpret_cast<T*>(store));
        }

        void Reset()
        {
            Get()->~T();
            isValid = false;
        }

    public:
        Optional() : isValid(false)
        {}

        Optional(OptionalNoType) : isValid(false)
        {}

        Optional(const T& value) : isValid(true)
        {
            new (store) T(value);
        }

        Optional(T&& value) : isValid(true)
        {
            new (store) T(sl::Move(value));
        }

        template<typename... Args>
        void Emplace(Args&&... args)
        {
            if (isValid)
                Reset();
            
            new (store) T(Forward<Args>(args)...);
        }

        ~Optional()
        {
            if (isValid)
                Reset();
        }

        Optional(const Optional& other) :isValid(other.isValid)
        {
            if (isValid)
                new (store) T(*other.Get());
        }

        Optional& operator=(const Optional& other)
        {
            if (other.isValid)
            {
                if (isValid)
                    *Get() = *other.Get();
                else
                {
                    isValid = true;
                    new (store) T(*other.Get());
                }
            }
            else
            {
                if (isValid)
                    Reset();
            }

            return *this;
        }

        Optional(Optional&& from) : isValid(from.isValid)
        {
            if (isValid)
                new (store) T(sl::Move(*from.Get()));
        }

        Optional& operator=(Optional&& from)
        {
            if (from.isValid)
            {
                if (isValid)
                    *Get() = sl::Move(*from.Get());
                else
                {
                    isValid = true;
                    new (store) T(sl::Move (*from.Get()));
                }
            }
            else
            {
                if (isValid)
                    Reset();
            }

            return *this;
        }

        template<typename U>
        Optional& operator=(const Optional<U>& other)
        {
            if (other.isValid)
            {
                if (isValid)
                    *Get() = *other.Get();
                else
                {
                    isValid = true;
                    new (store) T(*other.Get());
                }
            }
            else
            {
                if (isValid)
                    Reset();
            }

            return *this;
        }

        template<typename U>
        Optional& operator=(Optional<U>&& from)
        {
            if (from.isValid)
            {
                if (isValid)
                    *Get() = sl::Move(*from.Get());
                else
                {
                    isValid = true;
                    new (store) T(sl::Move (*from.Get()));
                }
            }
            else
            {
                if (isValid)
                    Reset();
            }

            return *this;
        }

        constexpr operator bool() const
        { return isValid; }

        constexpr bool HasValue() const
        { return isValid; }

        constexpr const T& operator*() const
        { return *Get(); }

        constexpr T& operator*()
        { return *Get(); }

        T* operator->()
        { return Get(); }

        T& Value()
        { return *Get(); }
    };

    //convinience operators
    template<typename A, typename B>
    constexpr bool operator==(const Optional<A>& opt, const B& value)
    { return opt ? (*opt == value) : false; }

    template<typename A, typename B>
    constexpr bool operator==(const A& value, const Optional<B>& opt)
    { return opt ? (value == *opt) : false; }

    template<typename A, typename B>
    constexpr bool operator!=(const Optional<A>& opt, const B& value)
    { return opt ? (*opt != value) : true; }

    template<typename A, typename B>
    constexpr bool operator!=(const A& value, const Optional<B>& opt)
    { return opt ? (*opt != value) : true; }

    template<typename T>
    using Opt = Optional<T>;
}
