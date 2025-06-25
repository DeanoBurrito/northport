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
        size_t frequency;
        size_t ticks;

        constexpr TimeCount() : frequency(0), ticks(0)
        {}

        constexpr TimeCount(size_t freq, size_t count) : frequency(freq), ticks(count)
        {}

        constexpr TimeCount(TimeScale freq, size_t count) : frequency(freq), ticks(count)
        {}

        TimeCount Rebase(size_t newFrequency) const;

        bool operator==(const TimeCount& other) const
        { return other.frequency == frequency && other.ticks == ticks; }

        bool operator!=(const TimeCount& other) const
        { return other.frequency != frequency || other.ticks != ticks; }
    };

    struct TimePoint
    {
        constexpr static sl::TimeScale Frequency = TimeScale::Nanos;

        size_t epoch;

        static TimePoint Now()
        { return {}; }

        bool operator==(TimePoint other)
        { return epoch == other.epoch; }

        bool operator!=(TimePoint other)
        { return epoch != other.epoch; }

        bool operator>(TimePoint other)
        { return epoch > other.epoch; }

        bool operator<(TimePoint other)
        { return epoch < other.epoch; }

        TimePoint operator+(TimeCount duration)
        { return { epoch + duration.Rebase(Frequency).ticks }; }
    };
}

constexpr sl::TimeCount operator""_ms(unsigned long long units)
{ return sl::TimeCount(sl::TimeScale::Millis, units); }

constexpr sl::TimeCount operator""_us(unsigned long long units)
{ return sl::TimeCount(sl::TimeScale::Micros, units); }

constexpr sl::TimeCount operator""_ns(unsigned long long units)
{ return sl::TimeCount(sl::TimeScale::Nanos, units); }

constexpr sl::TimeCount operator""_ps(unsigned long long units)
{ return sl::TimeCount(sl::TimeScale::Picos, units); }
