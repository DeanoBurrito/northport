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

        ScaledTime ToScale(TimeScale newScale);
    }; //TODO: apply this elsewhere where time is concerned, add util methods
}
