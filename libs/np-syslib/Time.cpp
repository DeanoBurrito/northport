#include <Time.h>

namespace sl
{
    ScaledTime ScaledTime::FromFrequency(size_t hertz)
    {
        ScaledTime time { TimeScale::Millis, (size_t)TimeScale::Millis / hertz };
        while (time.units == 0)
        {
            time.scale = (TimeScale)((size_t)time.scale * 1000);
            time.units = (size_t)time.scale / hertz;
        }

        return time;
    }
    
    ScaledTime ScaledTime::ToScale(TimeScale newScale) const
    {
        const size_t ratio = (size_t)newScale / (size_t)scale;
        return { newScale, units * ratio };
    }
}
