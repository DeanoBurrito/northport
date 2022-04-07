#pragma once

#include <stdint.h>
#include <stddef.h>


namespace Kernel::Devices
{
    enum class LocalApicRegister : uint16_t
    {
        Id = 0x20,
        Version = 0x30,
        TaskPriority = 0x80,
        ArbitrationPriority = 0x90,
        ProcessorPriority = 0xA0,
        EOI = 0xB0,
        RemoteRead = 0xC0,
        LocalDestination = 0xD0,
        DestinationFormat = 0xE0,
        SpuriousInterruptVector = 0xF0,

        //in-service registers
        ISR0 = 0x100,
        ISR1 = 0x110,
        ISR2 = 0x120,
        ISR3 = 0x130,
        ISR4 = 0x140,
        ISR5 = 0x150,
        ISR6 = 0x160,
        ISR7 = 0x170,
        
        //trigger mode registers
        TMR0 = 0x180,
        TMR1 = 0x190,
        TMR2 = 0x1A0,
        TMR3 = 0x1B0,
        TMR4 = 0x1C0,
        TMR5 = 0x1D0,
        TMR6 = 0x1E0,
        TMR7 = 0x1F0,

        //interrupt request registers
        IRR0 = 0x200,
        IRR1 = 0x210,
        IRR2 = 0x220,
        IRR3 = 0x230,
        IRR4 = 0x240,
        IRR5 = 0x250,
        IRR6 = 0x260,
        IRR7 = 0x270,

        ErrorStatus = 0x280,
        CmciLVT = 0x2F0, //corrected machine check interrupt
        ICR0 = 0x300,
        ICR1 = 0x310,
        TimerLVT = 0x320,
        ThermalSensorLVT = 0x330,
        PerfMonCountersLVT = 0x340,
        LocalInt0LVT = 0x350,
        LocalInt1LVT = 0x360,
        ErrorLVT = 0x370,

        TimerInitialCount = 0x380,
        TimerCurrentCount = 0x390,
        TimerDivisor = 0x3E0,
    };

    enum class ApicDeliveryMode : uint8_t
    {
        Fixed = 0,
        SMI = 0b010,
        NMI = 0b100,
        ExtINT = 0b111,
        INIT = 0b101,
        StartUp = 0b110,
    };

    enum class ApicTimerMode : uint8_t
    {
        OneShot = 0b00,
        Periodic = 0b01,
        TscDeadline = 0b10,
    };

    enum class ApicTimerDivisor : uint8_t
    {
        _1 = 0b1011,
        _2 = 0b0000,
        _4 = 0b0001,
        _8 = 0b0010,
        _16 = 0b0011,
        _32 = 0b1000,
        _128 = 0b1010,
    };

    struct LvtEntry
    {
        uint32_t raw;

        void Set(uint8_t vector);
        void Set(uint8_t vector, ApicDeliveryMode mode, bool levelTriggered);
        void Set(uint8_t vector, ApicDeliveryMode mode, bool activeLow, bool levelTriggered);
        void Set(uint8_t vector, ApicTimerMode mode);
    };

    class LApic
    {
    private:
        uint64_t baseAddress;
        uint32_t apicId;

        uint32_t calibratedDivisor;
        uint64_t timerTicksPerMs;

        bool x2ModeEnabled;

        void WriteReg(LocalApicRegister reg, uint32_t value) const;
        uint32_t ReadReg(LocalApicRegister reg) const;
        void CalibrateTimer();
    
    public:
        static LApic* Local();

        void Init();
        void SendEOI() const;
        bool IsBsp() const;
        size_t GetId() const;

        void SendIpi(uint32_t destId, uint8_t vector);
        void BroadcastIpi(uint8_t vector, bool includeSelf);
        void SendStartup(uint32_t destId, uint8_t vector);

        void SetLvtMasked(LocalApicRegister lvtReg, bool masked) const;
        bool GetLvtMasked(LocalApicRegister lvtReg) const;
        void SetupTimer(size_t millis, uint8_t vector, bool periodic);
        uint64_t GetTimerIntervalMS() const;
    };
}
