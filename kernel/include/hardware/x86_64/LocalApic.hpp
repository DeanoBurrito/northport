#pragma once

#include <KernelTypes.hpp>

namespace Npk
{
    void InitBspLapic(uintptr_t& virtBase);
    void SignalEoi();

    enum class IpiType
    {
        Fixed = 0b000,
        Nmi =   0b100,
        Init =  0b101,
        Sipi =  0b110,
    };

    void SendIpi(uint32_t dest, IpiType type, uint8_t vector);
}
