#pragma once

#include <stddef.h>

namespace sl
{
    enum class TimeScale : size_t
    {
        Millis = 1'000,
        Micros = 1'000'000,
        Nanos = 1'000'000'000,
    };

    struct ScaledTime
    {
        TimeScale scale;
        size_t units;

        constexpr ScaledTime() : scale(TimeScale::Nanos), units(0)
        {}

        constexpr ScaledTime(TimeScale s, size_t u) : scale(s), units(u)
        {}

        static ScaledTime FromFrequency(size_t hertz);

        ScaledTime ToScale(TimeScale newScale) const;

        [[gnu::always_inline]]
        inline size_t ToMillis() const
        { return ToScale(TimeScale::Millis).units; }

        [[gnu::always_inline]]
        inline size_t ToMicros() const
        { return ToScale(TimeScale::Micros).units; }

        [[gnu::always_inline]]
        inline size_t ToNanos() const
        { return ToScale(TimeScale::Nanos).units; }
    };

    struct TimePoint
    {
        size_t epoch;
        //TODO: populate and we'll likely want sl::Duration

        static TimePoint Now()
        { return {}; }
    };
}

constexpr sl::ScaledTime operator""_ms(unsigned long long units)
{ return sl::ScaledTime(sl::TimeScale::Millis, units); }

constexpr sl::ScaledTime operator""_us(unsigned long long units)
{ return sl::ScaledTime(sl::TimeScale::Micros, units); }

constexpr sl::ScaledTime operator""_ns(unsigned long long units)
{ return sl::ScaledTime(sl::TimeScale::Nanos, units); }
