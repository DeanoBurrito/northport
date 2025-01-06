#include <arch/Timers.h>
#include <arch/Misc.h>
#include <arch/x86_64/Timers.h>
#include <arch/x86_64/Apic.h>
#include <core/Log.h>
#include <services/AcpiTables.h>
#include <services/Vmm.h>
#include <ArchHints.h>
#include <NativePtr.h>
#include <Maths.h>
#include <Locks.h>

namespace Npk
{
    constexpr size_t PitPeriod = 838; //838.09511ns

    constexpr size_t FemtosPerNano = 1'000'000;
    constexpr size_t HpetRegCaps = 0x0;
    constexpr size_t HpetRegConfig = 0x10;
    constexpr size_t HpetRegCounter = 0xF0;

    sl::SpinLock pitLock;

    Services::VmObject* hpetVmo;
    sl::NativePtr hpetRegs;
    size_t hpetPeriod;

    void CalibrationTimersInit()
    {
        //TODO: would be nice to investigate the acpi timer as well

        using namespace Services;
        hpetRegs = nullptr;
        auto hpet = static_cast<const Hpet*>(FindAcpiTable(Services::SigHpet));
        if (hpet == nullptr)
            return;

        hpetVmo = Services::CreateMmioVmo(hpet->baseAddress.address, 0x1000, HatFlag::Mmio);
        VALIDATE_(hpetVmo != nullptr, );
        auto maybeHpetRegs = Services::VmAllocWired(hpetVmo, 0x1000, 0, VmViewFlag::Write);
        VALIDATE_(maybeHpetRegs.HasValue(), );
        hpetRegs = *maybeHpetRegs;

        //reset main counter and leave it enabled
        hpetRegs.Offset(HpetRegConfig).Write<uint64_t>(0);
        hpetRegs.Offset(HpetRegCounter).Write<uint64_t>(0);
        hpetRegs.Offset(HpetRegConfig).Write<uint64_t>(0b01); //bit0 = enable, bit1 = legacy routing
        
        hpetPeriod = hpetRegs.Offset(HpetRegCaps).Read<uint64_t>() >> 32;
        if (hpetPeriod > 0x05F5E100 || hpetPeriod <= FemtosPerNano)
        {
            Log("Invalid HPET period: %zu", LogLevel::Error, hpetPeriod);
            hpetRegs = nullptr;
            return;
        }
        Log("HPET avilable: period=%zuns, regs=%p (0x%" PRIx64")", LogLevel::Info,
            hpetPeriod / FemtosPerNano, hpetRegs.ptr, hpet->baseAddress.address);
    }

    static inline uint16_t PitRead()
    {
        Out8(PortPitCmd, 0); //latch channel 0
        uint16_t data = In8(PortPitData);
        data |= (uint16_t)In8(PortPitData) << 8;
        return data;
    }

    TimerTickNanos CalibrationSleep(TimerTickNanos nanos)
    {
        if (hpetRegs.ptr != nullptr)
        {
            const auto counter = hpetRegs.Offset(HpetRegCounter);
            const size_t begin = counter.Read<uint64_t>();
            const size_t target = begin + ((nanos * FemtosPerNano) / hpetPeriod);

            while (counter.Read<uint64_t>() < target)
                sl::HintSpinloop();

            return ((counter.Read<uint64_t>() - begin) * hpetPeriod) / FemtosPerNano;
        }
        else
        {
            const uint64_t target = 0xFFFF - (nanos / PitPeriod);

            sl::ScopedLock scopeLock(pitLock);
            Out8(PortPitCmd, 0x34);
            Out8(PortPitData, 0xFF);
            Out8(PortPitData, 0xFF);

            while (PitRead() > target)
                sl::HintSpinloop();

            return (0xFFFF - PitRead()) * PitPeriod;
        }
    }

    static inline LocalApic& LApic()
    {
        return *static_cast<LocalApic*>(GetLocalPtr(SubsysPtr::IntrCtrl));
    }

    void InitLocalTimers()
    {
        LApic().CalibrateTimer();
    }

    bool ArmIntrTimer(TimerTickNanos nanos)
    {
        LApic().ArmTimer(nanos, IntrVectorTimer);
        return true;
    }

    TimerTickNanos MaxIntrTimerExpiry()
    {
        return LApic().TimerMaxNanos();
    }

    TimerTickNanos ReadPollTimer()
    {
        return LApic().ReadTscNanos();
    }
}
