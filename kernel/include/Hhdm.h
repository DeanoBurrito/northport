#pragma once

#include <stdint.h>
#include <stddef.h>

namespace Npk
{
    extern uintptr_t hhdmBase;
    extern size_t hhdmLength;

    template<typename T>
    inline T AddHhdm(T value)
    { return reinterpret_cast<T>((uintptr_t)value + hhdmBase); }

    template<>
    inline uintptr_t AddHhdm(uintptr_t value)
    { return value + hhdmBase; }

    template<typename T>
    inline T SubHhdm(T value)
    { return reinterpret_cast<T>((uintptr_t)value - hhdmBase); }

    template<>
    inline uintptr_t SubHhdm(uintptr_t value)
    { return value - hhdmBase; }
}
