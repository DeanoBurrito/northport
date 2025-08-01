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

    CalendarPoint CalendarPoint::From(TimePoint input)
    {
        CalendarPoint p {};
        auto accum = input.epoch / TimePoint::Frequency;

        p.second = accum % 60;
        p.minute = (accum / 60) % 60;
        p.hour = (accum / 3600) % 24;

        accum = (accum / 86400) + 719468;

        const int era = (accum >= 0 ? accum : accum - 146096) / 146097;
        const auto doe = accum - era * 146097;
        const auto yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
        const int y = yoe + era * 400;
        const auto doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
        const auto mp = (5 * doy + 2) / 153;
        const auto d = doy - (153 * mp + 2) / 5 + 1;
        const auto m = mp + (mp < 10 ? 3 : -9);

        p.year = y + (m <= 2);
        p.month = m;
        p.dayOfMonth = d;

        p.dayOfWeek = accum >= -4 ? (accum + 4) % 7 : (accum + 5) % 7 + 6;

        const auto n1 = 275 * p.month / 9;
        const auto n2 = (p.month + 9) / 12;
        const auto n3 = (1 + (p.year - 4 * p.year / 4 + 2) / 3);
        p.dayOfYear = n1 - (n2 * n3) + p.dayOfMonth - 30;

        p.week = p.dayOfYear / 7;

        return p;
    }

    TimePoint CalendarPoint::ToTimePoint()
    {
        decltype(TimePoint::epoch) accum {};

        accum += second;
        accum += minute * 60;
        accum += hour * 3600;
        accum += dayOfYear * 86400;
        accum += (year - 70) * 31536000;
        accum += ((year - 69) / 4) * 86400;
        accum -= ((year - 1) / 100) * 86400;
        accum += ((year + 299) / 400) * 86400;

        return accum * TimePoint::Frequency;
    }
}
