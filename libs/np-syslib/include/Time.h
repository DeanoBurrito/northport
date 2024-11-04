#pragma once

#include <stddef.h>

namespace sl
{
    enum class TimeScale : size_t
    {
        Millis = 1000,
        Micros = 1000 * Millis,
        Nanos = 1000 * Micros,
        Picos = 1000 * Nanos,
        Femtos = 1000 * Picos
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

        [[gnu::always_inline]]
        inline size_t ToPicos() const
        { return ToScale(TimeScale::Picos).units; }

        [[gnu::always_inline]]
        inline size_t ToFemtos() const
        { return ToScale(TimeScale::Femtos).units; }
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

constexpr sl::ScaledTime operator""_ps(unsigned long long units)
{ return sl::ScaledTime(sl::TimeScale::Picos, units); }
