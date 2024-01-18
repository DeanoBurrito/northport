#pragma once

#include <interfaces/driver/Decorators.h>

constexpr inline npk_string operator""_apistr(const char* str, size_t len)
{
    return { .length = len, .data = str };
}
