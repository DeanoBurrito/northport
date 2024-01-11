#pragma once

#include <drivers/api/Decorators.h>

constexpr inline npk_string operator""_apistr(const char* str, size_t len)
{
    return { .length = len, .data = str };
}
