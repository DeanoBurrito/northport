#pragma once

#include <interfaces/driver/Input.h>
#include <Span.h>
#include <stdint.h>
#include <Optional.h>

namespace Ps2
{
    sl::Opt<npk_key_id> TryParseSet2Base(sl::Span<uint8_t> buffer);
    sl::Opt<npk_key_id> TryParseSet2Extended(sl::Span<uint8_t> buffer);
}
