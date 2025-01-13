#pragma once

#include <NativePtr.h>
#include <arch/Timers.h>
#include <services/VmPagers.h>

namespace Npk
{
    enum class LapicReg
    {
        Version = 0x30,
        Eoi = 0xB0,
        SpuriousConfig = 0xF0,
        ErrorStatus = 0x280,

        IcrLow = 0x300,
        IcrHigh = 0x310,
        LvtTimer = 0x320,
        LvtError = 0x370,

        TimerInitCount = 0x380,
        TimerCount = 0x390,
        TimerDivisor = 0x3E0,
    };

    class LocalApic
    {
    private:
        Services::VmObject* mmioVmo;
        sl::NativePtr mmio;
        size_t tscFrequency;
        size_t timerFrequency;
        bool x2Mode;
        bool useTscDeadline;

        uint32_t ReadReg(LapicReg reg);
        void WriteReg(LapicReg reg, uint32_t value);

    public:
        bool Init();
        void CalibrateTimer();
        TimerTickNanos ReadTscNanos();
        TimerTickNanos TimerMaxNanos();
        void ArmTimer(TimerTickNanos nanos, size_t vector);
        void SendEoi();
        void SendIpi(size_t destAddr);
    };

    void InitIoApics();
}
