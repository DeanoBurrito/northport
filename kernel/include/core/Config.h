#pragma once

#include <Span.h>

namespace Npk::Core
{
    void InitConfigStore(sl::StringSpan source);

    sl::StringSpan GetConfig(sl::StringSpan key);
    size_t GetConfigNumber(sl::StringSpan key, size_t orDefault);
}
