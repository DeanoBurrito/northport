#pragma once

#include <Types.h>

namespace sl
{
    enum TimeScale : size_t
    {
        Millis = 1000,
        Micros = 1000 * Millis,
        Nanos = 1000 * Micros,
        Picos = 1000 * Nanos,
        Femtos = 1000 * Picos
    };

    struct TimeCount
    {
        uint64_t frequency;
        uint64_t ticks;

        constexpr TimeCount() : frequency(0), ticks(0)
        {}

        constexpr TimeCount(size_t freq, size_t count) : frequency(freq), ticks(count)
        {}

        constexpr TimeCount(TimeScale freq, size_t count) : frequency(freq), ticks(count)
        {}

        TimeCount Rebase(size_t newFrequency) const;

        constexpr bool operator==(const TimeCount& other) const
        { return other.frequency == frequency && other.ticks == ticks; }

        constexpr bool operator!=(const TimeCount& other) const
        { return other.frequency != frequency || other.ticks != ticks; }
    };

    struct TimePoint
    {
        constexpr static sl::TimeScale Frequency = TimeScale::Nanos;

        uint64_t epoch;

        static TimePoint Now()
        { return {}; }

        constexpr bool operator==(TimePoint other)
        { return epoch == other.epoch; }

        constexpr bool operator!=(TimePoint other)
        { return epoch != other.epoch; }

        constexpr bool operator>(TimePoint other)
        { return epoch > other.epoch; }

        constexpr bool operator<(TimePoint other)
        { return epoch < other.epoch; }

        TimePoint operator+(TimeCount duration)
        { return { epoch + duration.Rebase(Frequency).ticks }; }

        constexpr TimeCount ToCount() const
        { return TimeCount(Frequency, epoch); }
    };

    constexpr TimePoint operator-(TimePoint a, TimePoint b)
    { return { a.epoch - b.epoch }; }

    constexpr TimePoint operator+(TimePoint a, TimePoint b)
    { return { a.epoch + b.epoch }; }
}

constexpr sl::TimeCount operator""_ms(unsigned long long units)
{ return sl::TimeCount(sl::TimeScale::Millis, units); }

constexpr sl::TimeCount operator""_us(unsigned long long units)
{ return sl::TimeCount(sl::TimeScale::Micros, units); }

constexpr sl::TimeCount operator""_ns(unsigned long long units)
{ return sl::TimeCount(sl::TimeScale::Nanos, units); }

constexpr sl::TimeCount operator""_ps(unsigned long long units)
{ return sl::TimeCount(sl::TimeScale::Picos, units); }
