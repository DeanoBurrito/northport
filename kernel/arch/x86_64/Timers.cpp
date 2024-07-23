#include <arch/Timers.h>
#include <arch/x86_64/Cpuid.h>
#include <arch/x86_64/Apic.h>
#include <config/AcpiTables.h>
#include <debug/Log.h>
#include <memory/VmObject.h>
#include <interrupts/Router.h>

namespace Npk
{
    constexpr const char* TimerStrs[] = 
    { "pit", "hpet", "lapic", "tsc" };

    enum class TimerName : size_t
    {
        Pit = 0,
        Hpet,
        Lapic,
        Tsc,
    };

    TimerName selectedSysTimer;
    TimerName selectedPollTimer;
    
    constexpr size_t FemtosPerNano = 1'000'000;
    constexpr size_t PitPeriod = 838; //838.09511ns

    constexpr size_t HpetRegCaps = 0x0;
    constexpr size_t HpetRegConfig = 0x10;
    constexpr size_t HpetRegCounter = 0xF0;
    constexpr size_t HpetRegT0Config = 0x100;
    constexpr size_t HpetRegT0Comparator = 0x108;
    
    VmObject hpetRegs;
    size_t hpetPeriod;
    size_t tscTicksPerMs;

    InterruptRoute timerIntrRoute;
    uint8_t timerIoApicPin;

    inline uint16_t PitRead()
    {
        Out8(PortPitCmd, 0); //latch channel 0
        uint16_t data = In8(PortPitData);
        data |= (uint16_t)In8(PortPitData) << 8;
        return data;
    }

    inline uint64_t TscRead()
    {
        uint64_t low, high;
        asm volatile("lfence; rdtsc" : "=a"(low), "=d"(high) :: "memory");
        return low | (high << 32);
    }

    static void PitSleep(size_t nanos)
    {
        ASSERT((nanos / PitPeriod) < 0xFFFF, "Sleep time too long for PIT.");
        const uint16_t target = 0xFFFF - (nanos / PitPeriod);

        //reset PIT counter to max
        Out8(PortPitCmd, 0x34);
        Out8(PortPitData, 0xFF);
        Out8(PortPitData, 0xFF);

        while (PitRead() > target)
            sl::HintSpinloop();
    }

    static void HpetSleep(size_t nanos)
    {
        ASSERT(hpetRegs.Valid(), "No HPET sleep: device is not available.");
        
        const sl::NativePtr counter = hpetRegs->Offset(HpetRegCounter);
        const size_t target = counter.Read<uint64_t>() + ((nanos * FemtosPerNano) / hpetPeriod);

        while (counter.Read<uint64_t>() < target)
            sl::HintSpinloop();
    }

    void PolledSleep(size_t nanoseconds)
    {
        if (hpetRegs.Valid())
            HpetSleep(nanoseconds);
        else
            PitSleep(nanoseconds);
    }

    static void InitTsc()
    {
        if (!CpuHasFeature(CpuFeature::Tsc))
            return;
        if (!CpuHasFeature(CpuFeature::InvariantTsc))
            Log("Invariant TSC not supported on this cpu.", LogLevel::Warning);
        
        constexpr size_t CalibRuns = 8;
        constexpr size_t CalibMillis = 10;
        long calibTimes[CalibRuns];

        for (size_t i = 0; i < CalibRuns; i++)
        {
            const size_t begin = TscRead();
            PolledSleep(CalibMillis * 1'000'000);
            const size_t end = TscRead();

            calibTimes[i] = (long)((end - begin) / CalibMillis);
        }

        auto finalTime = CoalesceTimerRuns(calibTimes, CalibRuns, 3);
        if (!finalTime)
            return;
        
        selectedPollTimer = TimerName::Tsc;
        tscTicksPerMs = *finalTime;

        if (CpuHasFeature(CpuFeature::TscDeadline))
            selectedSysTimer = TimerName::Tsc;
    }

    void InitLocalTimers()
    {}

    void InitGlobalTimers()
    {
        timerIoApicPin = 0;
        selectedSysTimer = (TimerName)-1;
        timerIntrRoute.Callback = nullptr;
        timerIntrRoute.dpc = nullptr;
        ASSERT_(AddInterruptRoute(&timerIntrRoute, CoreLocal().id));

        if (auto maybeHpet = Config::FindAcpiTable(Config::SigHpet); maybeHpet.HasValue())
        {
            const Config::Hpet* hpetTable = static_cast<const Config::Hpet*>(*maybeHpet);
            hpetRegs = VmObject {0x1000, hpetTable->baseAddress.address, VmFlag::Write | VmFlag::Mmio};

            //reset main counter and leave it enabled
            hpetRegs->Offset(HpetRegConfig).Write<uint64_t>(0);
            hpetRegs->Offset(HpetRegCounter).Write<uint64_t>(0);
            hpetRegs->Offset(HpetRegConfig).Write<uint64_t>(0b01); //bit 0 = enable bit, bit 1 = legacy routing
            hpetPeriod = hpetRegs->Offset(HpetRegCaps).Read<uint64_t>() >> 32;

            ASSERT(hpetPeriod <= 0x05F5E100, "Bad HPET period."); //magic number pulled from hpet spec
            ASSERT(hpetPeriod > FemtosPerNano, "HPET period < 1ns.");
            Log("Hpet available: period=%luns, regs=0x%lx (0x%lx)", LogLevel::Info, 
                hpetPeriod / FemtosPerNano, hpetRegs->raw, hpetTable->baseAddress.address);
            
            selectedPollTimer = TimerName::Hpet;
        }
        else
            selectedPollTimer = TimerName::Pit;
        
        InitTsc();
    }

    void InitInterruptTimer()
    {
        if (LocalApic::Local().CalibrateTimer())
        {
            selectedSysTimer = TimerName::Lapic;
            return;
        }
        
        if (!hpetRegs.Valid())
        {
            selectedSysTimer = TimerName::Pit;
            IoApic::Route(timerIoApicPin, timerIntrRoute.vector, timerIntrRoute.core, TriggerMode::Edge, PinPolarity::High, true);
            Log("PIT will be used for sys timer :(", LogLevel::Info);
        }
        else
        {
            //setup HPET comparator 0 for interrupts
            uint64_t configWord = hpetRegs->Offset(HpetRegT0Config).Read<uint64_t>();
            uint32_t allowedRoutes = configWord >> 32;
            while ((allowedRoutes & 1) == 0)
            {
                timerIoApicPin++;
                allowedRoutes >>= 1;
            }

            configWord &= ~(0b1111ul << 9); //zero routing field before writing
            configWord |= timerIoApicPin << 9;
            configWord &= ~(1ul << 3); //clear type bit = one shot timer
            configWord |= 1ul << 2; //enable interrupts
            configWord &= ~(1ul << 1); //edge triggered
            hpetRegs->Offset(HpetRegT0Config).Write(configWord);
            ASSERT(hpetRegs->Offset(HpetRegT0Config).Read<uint64_t>() == configWord, "HPET comparator 0 readback incorrect.");

            selectedSysTimer = TimerName::Hpet;
            IoApic::Route(timerIoApicPin, timerIntrRoute.vector, timerIntrRoute.core, TriggerMode::Edge, PinPolarity::High, true);
            Log("Hpet comparator 0 available for sys timer: vector=0x%lx, ioapicPin=%u.", 
                LogLevel::Info, timerIntrRoute.vector, timerIoApicPin);
        }
    }

    void ArmInterruptTimer(size_t nanoseconds, bool (*callback)(void*))
    {
        if ((size_t)selectedSysTimer == -1ul)
            InitInterruptTimer();

        if (callback != nullptr)
            timerIntrRoute.Callback = callback;
        
        switch (selectedSysTimer)
        {
        case TimerName::Pit:
        {
            ASSERT((nanoseconds / PitPeriod) < 0xFFFF, "Sleep time too long for PIT.");

            const uint16_t target = nanoseconds / PitPeriod;
            //mode 0, sets output high (level + high pintrigger) when count reached.
            Out8(PortPitCmd, 0x30); //mode 0, one-shot edge triggered (active high) timer.
            Out8(PortPitData, target);
            Out8(PortPitData, target >> 8);

            IoApic::Mask(timerIoApicPin, false);
            return;
        }

        case TimerName::Hpet:
        {
            const uint64_t mainCounter = hpetRegs->Offset(HpetRegCounter).Read<uint64_t>();
            const uint64_t target = mainCounter + ((nanoseconds * FemtosPerNano) / hpetPeriod);
            hpetRegs->Offset(HpetRegT0Comparator).Write(target);
            
            IoApic::Mask(timerIoApicPin, false);
            return;
        }

        case TimerName::Lapic:
            LocalApic::Local().SetTimer(false, nanoseconds, timerIntrRoute.vector);
            return;

        case TimerName::Tsc:
            LocalApic::Local().SetTimer(true, nanoseconds / 1'000'000 * tscTicksPerMs, timerIntrRoute.vector);
            return;
        
        default:
            ASSERT_UNREACHABLE();
        }
    }

    size_t InterruptTimerMaxNanos()
    {
        return -1ul;
        switch (selectedSysTimer)
        {
        case TimerName::Pit:
            return PitPeriod * 0xFFFF;
        case TimerName::Hpet:
        {
            //HPET period should always be >= 1ns, we assert this at init
            const bool is64Bit = hpetRegs->Offset(HpetRegCaps).Read<uint64_t>() & (1 << 13);
            if (is64Bit)
                return (size_t)-1ul;
            
            //32bit counter, how much time can it encode?
            return hpetPeriod * 0xFFFF'FFFF / 1000;
        }
        case TimerName::Lapic:
            return LocalApic::Local().MaxTimerNanos();
        case TimerName::Tsc:
        {
            if (tscTicksPerMs < 1'000'000)
                return -1ul;
            ASSERT_UNREACHABLE(); //TODO: maths, ugh
        }
        }
    }


    size_t PollTimer()
    {
        switch (selectedPollTimer)
        {
        case TimerName::Pit:
            return PitRead();
        case TimerName::Hpet:
            return hpetRegs->Offset(HpetRegCounter).Read<uint64_t>();
        case TimerName::Tsc:
            return TscRead();
        default:
            ASSERT_UNREACHABLE();
        }
    }

    size_t PollTicksToNanos(size_t ticks)
    {
        switch (selectedPollTimer)
        {
        case TimerName::Pit:
            return ticks * PitPeriod;
        case TimerName::Hpet:
            return (ticks * hpetPeriod) / FemtosPerNano;
        case TimerName::Tsc:
            return (ticks / tscTicksPerMs) * 1'000'000;
        default:
            ASSERT_UNREACHABLE();
        }
    }
    
    const char* InterruptTimerName()
    {
        return TimerStrs[(size_t)selectedSysTimer];
    }

    const char* PollTimerName()
    {
        return TimerStrs[(size_t)selectedPollTimer];
    }
}
