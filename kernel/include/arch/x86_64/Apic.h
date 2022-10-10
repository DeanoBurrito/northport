#pragma once

#include <NativePtr.h>
#include <Locks.h>

namespace Npk
{
    enum class PinPolarity : uint8_t
    {
        High = 0,
        Low = 1,
    };

    enum class TriggerMode : uint8_t
    {
        Edge = 0,
        Level = 1,
    };

    enum class LApicReg : size_t
    {
        Id = 0x20,
        Version = 0x30,
        EOI = 0xB0,
        SpuriousVector = 0xF0,

        Icr0 = 0x300,
        Icr1 = 0x310,
        LvtTimer = 0x320,
        LvtThermalSensor = 0x330,
        LvtPerfMon = 0x340,
        LvtLInt0 = 0x350,
        LvtLInt1 = 0x360,
        LvtError = 0x370,

        TimerInitCount = 0x380,
        TimerCount = 0x390,
        TimerDivisor = 0x3E0,
    };
    
    class LocalApic
    {
    private:
        bool inX2mode;
        bool tscForTimer;
        uint32_t id;
        sl::NativePtr mmio;
        size_t ticksPerMs;
        sl::SpinLock lock;

        static size_t timerVector;

        uint32_t ReadReg(LApicReg reg) const;
        void WriteReg(LApicReg reg, uint32_t value) const;
        bool ApplyTimerCalibration(long* runs, size_t runCount, size_t failThreshold);

        uint64_t ReadTsc() const;

    public:
        static LocalApic& Local();

        void Init();
        bool CalibrateTimer();

        void SetTimer(size_t nanoseconds, void (*callback)(size_t));
        void SendEoi() const;
    };

    enum class IoApicReg : uint32_t
    {
        Id = 0,
        Version = 1,
        TableBase = 0x10,
    };

    class IoApic
    {
    private:
        sl::NativePtr mmio;
        size_t gsiBase;
        size_t inputCount;
        sl::SpinLock lock;

        uint32_t ReadReg(IoApicReg reg) const;
        void WriteReg(IoApicReg reg, uint32_t value) const;
        uint64_t ReadRedirect(size_t index) const;
        void WriteRedirect(size_t index, uint64_t value) const;

    public:
        static void InitAll();

        static bool Route(uint8_t irqNum, uint8_t destVector, size_t destCpu, TriggerMode mode, PinPolarity pol, bool masked);
        static bool Masked(uint8_t irqNum);
        static void Mask(uint8_t irqNum, bool masked);
    };
}
