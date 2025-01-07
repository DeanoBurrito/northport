#include <Time.h>

namespace sl
{
    TimeCount TimeCount::Rebase(size_t newFrequency) const
    {
        if (newFrequency == frequency)
            return *this;
        if (frequency == 0 || newFrequency == 0)
            return { 0, 0 };

        return TimeCount(newFrequency, ticks * newFrequency / frequency); //TODO: we should check for saturation here
    }
}
