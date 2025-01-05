#pragma once

#include <Types.h>
#include <CppUtils.h>
#include <PlacementNew.h>

namespace sl
{
    struct NoErrorType
    {};

    constexpr static inline NoErrorType NoError;

    template<typename T, typename E, bool TrivialDtor>
    struct DtorCrtp;

    //TODO: might be nice to have a 'live' version of this that does runtime checks
    //when trying to access the error or value that its the correct access type?
    template<typename T, typename E>
    class ErrorOr
    {
    friend struct DtorCrtp<T, E, true>;
    private:
        alignas(T) uint8_t store[sizeof(T)];
        E err;

        const T* Get() const
        {
            return sl::Launder(reinterpret_cast<const T*>(store));
        }

        T* Get()
        {
            return sl::Launder(reinterpret_cast<T*>(store));
        }

    public:
        constexpr ErrorOr() : err {}
        {
            new (store) T{};
        }

        constexpr ErrorOr(NoErrorType) : err {}
        {
            new (store) T{};
        }

        constexpr ErrorOr(E init) : err { init }
        {
            if (init == E{})
                new (store) T{};
        }

        ErrorOr(T value) : err {}
        {
            new (store) T{ sl::Move(value) };
        }

        ErrorOr(const ErrorOr& other) : err { other.err }
        {
            if (HasError())
                return;
            new (store) T { *sl::Launder(reinterpret_cast<const T*>(other.store)) };
        }

        ErrorOr& operator=(const ErrorOr& other)
        {
            if (other.HasValue())
            {
                T temp { *sl::Launder(reinterpret_cast<T*>(other.store)) };
                if (HasValue())
                    sl::Launder(reinterpret_cast<T*>(store))->~T();
                err = other.err;
                new (store) T{ sl::Move(temp) };
            }
            else
            {
                if (HasValue())
                    sl::Launder(reinterpret_cast<T*>(store))->~T();
                err = other.err;
            }

            return *this;
        }

        ErrorOr(ErrorOr&& from)
        {
            if (HasError())
                return;

            new (store) T { sl::Move(*sl::Launder(reinterpret_cast<const T*>(from.store))) };
        }

        ErrorOr& operator=(ErrorOr&& from)
        {
            if (from.HasError())
            {
                T temp { sl::Move(*sl::Launder(reinterpret_cast<T*>(from.store))) };
                if (HasValue())
                    sl::Launder(reinterpret_cast<T*>(store))->~T();
                err = from.err;
                new (store) T{ sl::Move(temp) };
            }
            else
            {
                if (HasValue())
                    sl::Launder(reinterpret_cast<T*>(store))->~T();
                err = from.err;
            }

            return *this;
        }

        constexpr bool HasValue() const
        { 
            return err == E{};
        }

        constexpr bool HasError() const
        {
            return !HasValue();
        }

        constexpr const T& operator*() const
        { 
            return *Get(); 
        }

        constexpr T& operator*()
        { 
            return *Get();
        }

        T* operator->()
        { 
            return Get();
        }

        T& Value()
        { 
            return *Get(); 
        }

        E Error()
        {
            return err;
        }
    };

    template<typename T, typename E>
    struct DtorCrtp<T, E, true> {};

    template<typename T, typename E>
    struct DtorCrtp<T, E, false>
    {
        ~DtorCrtp()
        {
            auto store = static_cast<ErrorOr<T, E>*>(this);
            if (store->HasValue())
                sl::Launder(reinterpret_cast<T*>(store->store))->~T();
        }
    };

    template<typename E>
    class ErrorOr<void, E>
    {
    private:
        E err;

    public:
        constexpr ErrorOr() : err {}
        {}

        constexpr ErrorOr(NoErrorType) : err {}
        {}

        constexpr ErrorOr(E init) : err { init }
        {}

        constexpr bool HasValue() const
        { 
            return err == E{};
        }

        constexpr bool HasError() const
        {
            return !HasValue();
        }

        E Error()
        {
            return err;
        }
    };
}
