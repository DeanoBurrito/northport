#pragma once

#include <Maths.hpp>

namespace sl
{
    template<typename T, bool BigEndian>
    class EndianAware
    {
    private:
        T value;

    public:
        constexpr EndianAware() noexcept : value {}
        {}

        constexpr EndianAware(T init) noexcept
        {
            Store(init);
        }

        operator T() const noexcept
        { return Load(); }

        operator T() const volatile noexcept
        { return Load(); }

        T operator=(T incoming) noexcept
        { Store(incoming); return Load(); }

        T operator=(T incoming) volatile noexcept
        { Store(incoming); return Load(); }

        void Store(T incoming)
        {
            if constexpr (BigEndian)
                value = HostToBe(incoming);
            else
                value = HostToLe(incoming);
        }

        void Store(T incoming) volatile
        {
            if constexpr (BigEndian)
                value = HostToBe(incoming);
            else
                value = HostToLe(incoming);
        }

        T Load() const
        {
            if constexpr (BigEndian)
                return BeToHost(value);
            else
                return LeToHost(value);
        }

        T Load() const volatile
        {
            if constexpr (BigEndian)
                return BeToHost(value);
            else
                return LeToHost(value);
        }
    };

    using Le8 = EndianAware<uint8_t, false>;
    using Le16 = EndianAware<uint16_t, false>;
    using Le32 = EndianAware<uint32_t, false>;
    using Le64 = EndianAware<uint64_t, false>;

    using Be8 = EndianAware<uint8_t, true>;
    using Be16 = EndianAware<uint16_t, true>;
    using Be32 = EndianAware<uint32_t, true>;
    using Be64 = EndianAware<uint64_t, true>;
}
