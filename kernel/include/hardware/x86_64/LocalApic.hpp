#pragma once

#include <CoreTypes.hpp>

namespace Npk
{
    static_assert(DebugEventVector == 0xFB);
    constexpr uint8_t LapicErrorVector = 0xFC;
    constexpr uint8_t LapicIpiVector = 0xFD;
    constexpr uint8_t LapicTimerVector = 0xFE;
    constexpr uint8_t LapicSpuriousVector = 0xFF;

    void InitBspLapic(uintptr_t& virtBase);
    void InitApLapic();
    void SignalEoi();
    uint32_t MyLapicId();
    uint8_t MyLapicVersion();

    void ArmTscInterrupt(uint64_t expiry);
    void HandleLapicTimerInterrupt();
    void HandleLapicErrorInterrupt();

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
