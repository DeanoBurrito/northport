#include <Time.h>

namespace sl
{
    ScaledTime ScaledTime::FromFrequency(size_t hertz)
    {
        ScaledTime time { TimeScale::Millis, (size_t)TimeScale::Millis / hertz };
        while (time.units < 1000 && time.scale != TimeScale::Femtos)
        {
            time.scale = (TimeScale)((size_t)time.scale * 1000);
            time.units = (size_t)time.scale / hertz;
        }

        return time;
    }

    ScaledTime ScaledTime::ToScale(TimeScale newScale) const
    {
        if (newScale == scale)
            return *this;

        const size_t newScaleDigits = static_cast<size_t>(newScale);
        const size_t prevScaleDigits = static_cast<size_t>(scale);
        if (newScaleDigits < prevScaleDigits)
            return { newScale, units / (prevScaleDigits / newScaleDigits) };
        else
            return { newScale, units * (newScaleDigits / prevScaleDigits) };
    }
}
