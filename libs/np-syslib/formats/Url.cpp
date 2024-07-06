#include <formats/Url.h>
#include <Memory.h>

namespace sl
{
    StringSpan Url::GetNextSeg(StringSpan prev) const
    {
        auto* begin = prev.Empty() ? path.Begin() : prev.End() + 1;
        if (begin >= path.End())
            return {};

        if (*begin == '/')
            begin++;
        auto* scan = begin;

        size_t count = 0;
        while (count < path.Size() && *scan != '/' && *scan != 0)
        {
            scan++;
            count++;
        }

        return StringSpan(begin, count);
    }

    Url Url::Parse(StringSpan input)
    {
        //remove trailing NULLs from input
        while (*(input.End() - 1) == 0)
            input = input.Subspan(0, input.Size() - 1);
        
        auto* buffer = input.Begin();
        auto LengthUntil = [=](size_t start, char target) -> size_t
        {
            const size_t foundAt = sl::memfirst(buffer + start, target, input.Size() - start);
            if (foundAt == input.Size() - start)
                return 0;
            return foundAt;
        };

        Url url {};
        url.source = input;

        size_t start = 0;
        url.scheme = StringSpan(buffer, LengthUntil(start, ':'));
        start += url.scheme.Size() + (url.scheme.Empty() ? 0 : 1);

        if (buffer[start] == '/' && buffer[start + 1] == '/')
        {
            start += 2;
            url.user = StringSpan(buffer + start, LengthUntil(start, '@'));
            start += url.user.Size() + (url.user.Empty() ? 0 : 1);

            url.host = StringSpan(buffer + start, LengthUntil(start, ':'));
            start += url.host.Size() + (url.host.Empty() ? 0 : 1);
            if (url.host.Empty()) //there's no port component
            {
                url.host = StringSpan(buffer + start, LengthUntil(start, '/'));
                start += url.host.Size() + (url.host.Empty() ? 0 : 1);
                url.port = {};
            }
            else
            {
                url.port = StringSpan(buffer + start, LengthUntil(start, '/'));
                start += url.port.Size() + (url.port.Empty() ? 0 : 1);
            }
        }
        else
            url.user = url.host = url.port = {};

        url.path = StringSpan(buffer + start, LengthUntil(start, '?'));
        auto& next = url.path.Empty() ? url.path : url.query;
        next = StringSpan(buffer + start, LengthUntil(start, '#'));
        start += next.Size() + (next.Empty() ? 0 : 1);
        
        next = url.path.Empty() ? url.path : url.fragment;
        next = StringSpan(buffer + start, input.Size() - start);
        
        return url;
    }
}
