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
}
