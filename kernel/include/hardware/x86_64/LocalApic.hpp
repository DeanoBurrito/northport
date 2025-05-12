#pragma once

#include <KernelTypes.hpp>

namespace Npk
{
    void InitBspLapic(uintptr_t& virtBase);
    void SignalEoi();
    uint32_t MyLapicId();

    enum class IpiType
    {
        Fixed = 0b000,
        Nmi =   0b100,
        Init =  0b101,
        Startup =  0b110,

        InitDeAssert = 9999,
    };

    void SendIpi(uint32_t dest, IpiType type, uint8_t vector);
    bool LastIpiSent();
}
