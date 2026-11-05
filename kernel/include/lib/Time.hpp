#pragma once

#include "Types.hpp"
#include "Compiler.hpp"

namespace sl
{
    enum TimeScale : uint64_t
    {
        Millis = 1000,
        Micros = 1000 * Millis,
        Nanos = 1000 * Micros,
        Picos = 1000 * Nanos,
        Femtos = 1000 * Picos
    };

    struct Uint128
    {
        uint64_t low;
        uint64_t high;
    };

    SL_ALWAYS_INLINE
    constexpr Uint128 WideMultiply(uint64_t a, uint64_t b)
    {
#ifdef SL_HAS_UINT128
        const uint128_t product = static_cast<uint128_t>(a) * b;

        return { (uint64_t)product, (uint64_t)(product >> 64) };
#else
        constexpr uint64_t LowMask = 0xFFFF'FFFF;

        const uint64_t aHi = a >> 32;
        const uint64_t aLo = a & LowMask;
        const uint64_t bHi = b >> 32;
        const uint64_t bLo = b & LowMask;

        const uint64_t ll = aLo * bLo;
        const uint64_t hl = aHi * bLo;
        const uint64_t lh = aLo * bHi;
        const uint64_t hh = aHi * bHi;

        const uint64_t mid = (ll >> 32) + (hl & LowMask) + (lh & LowMask);

        return { (mid << 32) | (ll & LowMask),
            hh + (hl >> 32) + (lh >> 32) + (mid >> 32) };
#endif
    }

    SL_ALWAYS_INLINE
    constexpr uint64_t MulShift(uint64_t value, uint64_t mul, unsigned shift)
    {
        auto product = WideMultiply(value, mul);

        if (shift == 0)
            return product.high != 0 ? ~0ull : product.low;

        //nearest neighbour rounding for symmetry with TimeCount::Rebase().
        const uint64_t half = 1ull << (shift - 1);
        const uint64_t sum = product.low + half;
        if (sum < product.low)
        {
            if (product.high == ~0ull)
                return ~0ull;
            product.high++;
        }
        product.low = sum;

        if ((product.high >> shift) != 0)
            return ~0ull;

        return (product.high << (64 - shift)) | (product.low >> shift);
    }

    /* Ratio between two frequencies stored in a nicer format, allows
     * converting to the target frequency without division.
     */
    struct TimeConversion
    {
        uint64_t mul;
        unsigned shift;

        constexpr static TimeConversion Create(uint64_t from, uint64_t to)
        {
            if (from == 0 || to == 0)
                return { 0, 0 };

            const unsigned shift = SL_LEADING_ZEROS_64(to);
            const uint64_t numerator = to << shift;

            const uint64_t addend = from / 2;
            if (numerator > ~0ull - addend)
                return { numerator / from, shift };

            return { (numerator + addend) / from, shift };
        }

        SL_ALWAYS_INLINE
        constexpr uint64_t Apply(uint64_t ticks) const
        {
            return MulShift(ticks, mul, shift);
        }
    };

    struct TimeCount
    {
        uint64_t frequency;
        uint64_t ticks;

        constexpr TimeCount() 
            : frequency(0), ticks(0)
        {}

        constexpr TimeCount(uint64_t freq, uint64_t count) 
            : frequency(freq), ticks(count)
        {}

        constexpr TimeCount(TimeScale freq, uint64_t count) 
            : frequency(freq), ticks(count)
        {}

        constexpr TimeCount Rebase(uint64_t newFrequency) const
        {
            if (newFrequency == frequency)
                return *this;
            if (frequency == 0 || newFrequency == 0)
                return { newFrequency, 0 };

            const uint64_t whole = ticks / frequency;
            const uint64_t rem = ticks % frequency;

            uint64_t accum = rem * newFrequency;
            accum += frequency / 2;
            accum /= frequency;
            accum += whole * newFrequency;

            return TimeCount(newFrequency, accum);
        }

        constexpr TimeConversion ConversionTo(uint64_t newFrequency) const
        {
            return TimeConversion::Create(frequency, newFrequency);
        }

        constexpr bool operator==(const TimeCount& other) const
        { 
            return other.frequency == frequency && other.ticks == ticks;
        }

        constexpr bool operator!=(const TimeCount& other) const
        { 
            return other.frequency != frequency || other.ticks != ticks;
        }
    };

    constexpr static TimeCount NoTimeout { 1, ~0ull };

    struct TimePoint
    {
        constexpr static sl::TimeScale Frequency = TimeScale::Nanos;

        uint64_t epoch;

        TimePoint() = default;

        constexpr TimePoint(uint64_t value) 
            : epoch(value)
        {}

        constexpr bool operator==(TimePoint other) const 
        { 
            return epoch == other.epoch;
        }

        constexpr bool operator!=(TimePoint other) const
        { 
            return epoch != other.epoch;
        }

        constexpr bool operator>(TimePoint other) const
        { 
            return epoch > other.epoch;
        }

        constexpr bool operator<(TimePoint other) const
        { 
            return epoch < other.epoch;
        }

        constexpr bool operator<=(TimePoint other) const
        {
            return epoch <= other.epoch;
        }

        constexpr TimePoint operator+(TimeCount duration) const
        { 
            return { epoch + duration.Rebase(Frequency).ticks };
        }

        constexpr TimeCount ToCount() const
        { 
            return TimeCount(Frequency, epoch);
        }
    };

    constexpr TimePoint operator-(TimePoint a, TimePoint b)
    { 
        return { a.epoch - b.epoch }; 
    }

    constexpr TimePoint operator+(TimePoint a, TimePoint b)
    { 
        return { a.epoch + b.epoch }; 
    }

    struct CalendarPoint
    {
        uint32_t year;
        uint16_t dayOfYear;
        uint8_t dayOfMonth;
        uint8_t dayOfWeek;
        uint8_t month;
        uint8_t week;
        uint8_t hour;
        uint8_t minute;
        uint8_t second;

        static CalendarPoint From(TimePoint input);

        CalendarPoint() = default;

        TimePoint ToTimePoint();
    };
}

constexpr sl::TimeCount operator""_ms(unsigned long long units)
{ 
    return sl::TimeCount(sl::TimeScale::Millis, units); 
}

constexpr sl::TimeCount operator""_us(unsigned long long units)
{ 
    return sl::TimeCount(sl::TimeScale::Micros, units); 
}

constexpr sl::TimeCount operator""_ns(unsigned long long units)
{ 
    return sl::TimeCount(sl::TimeScale::Nanos, units);
}

constexpr sl::TimeCount operator""_ps(unsigned long long units)
{ 
    return sl::TimeCount(sl::TimeScale::Picos, units);
}
