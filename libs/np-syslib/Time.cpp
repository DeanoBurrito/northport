#include <Time.hpp>
#include <HostApi.hpp>

namespace sl
{
    TimePoint TimePoint::Now()
    {
        TimePoint tp {};

        if (!SlHostGetCurrentTime(tp))
            SL_EMIT_ERROR_HERE();

        return tp;
    }

    TimeCount TimeCount::Rebase(size_t newFrequency) const
    {
        if (newFrequency == frequency)
            return *this;
        if (frequency == 0 || newFrequency == 0)
            return { newFrequency, 0 };

        const auto q = ticks / frequency;
        const auto r = ticks % frequency;

        auto accum = r * newFrequency;
        accum += frequency / 2;
        accum /= frequency;
        accum += (q * newFrequency);

        return TimeCount(newFrequency, accum);
    }

    CalendarPoint CalendarPoint::Now()
    {
        CalendarPoint cp {};

        if (!SlHostGetCurrentDate(cp))
            SL_EMIT_ERROR_HERE();

        return cp;
    }

/* The following functions (CalendarPoint::From and CalendarPoint::ToTimePoint)
 * use code adapted from mlibc (https://github.com/managarm/mlibc). License text
 * is attached below.
 *
 * ----------------------------------------------------------------------------
 * Copyright (C) 2015-2025 mlibc Contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is furnished to do
 * so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
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

        //p.dayOfWeek = accum >= -4 ? (accum + 4) % 7 : (accum + 5) % 7 + 6;
        p.dayOfWeek = (accum + 4) % 7;

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
