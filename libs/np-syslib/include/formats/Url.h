#pragma once

#include <Span.h>

namespace sl
{
    struct Url
    {
        StringSpan source;
        StringSpan scheme;
        StringSpan user;
        StringSpan host;
        StringSpan port;
        StringSpan path;
        StringSpan query;
        StringSpan fragment;

        StringSpan GetNextSeg(StringSpan prev = {}) const;
        static Url Parse(StringSpan input);
    };
}
