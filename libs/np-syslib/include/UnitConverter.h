#pragma once

#include <stddef.h>

namespace sl
{
    struct UnitConversion
    {
        size_t major;
        size_t minor;
        const char* prefix;
    };

    enum class UnitBase
    {
        Binary = 1024,
        Decimal = 1000,
    };

    UnitConversion ConvertUnits(size_t input, UnitBase base = UnitBase::Decimal);
}
