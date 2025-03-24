#include <core/Acpi.h>
#include <core/Log.h>
#include <hardware/Arch.h>
#include <hardware/Platform.h>
#include <core/Defs.h>
#include <NativePtr.h>
#include <Locks.h>

namespace Npk
{
    constexpr size_t PitPeriod = 838; //838.09511ns

    constexpr size_t FemtosPerNano = 1'000'000;
    constexpr size_t HpetRegCaps = 0x0;
    constexpr size_t HpetRegConfig = 0x10;
    constexpr size_t HpetRegCounter = 0xF0;

    sl::SpinLock pitLock;

    sl::NativePtr hpetRegs;
    size_t hpetPeriod;

    void CalibrationTimersInit(uintptr_t hpetMmioBase)
    {
        //TODO: would be nice to investigate the acpi timer as well

        using namespace Core;
        Hpet hpet {};
        auto hpetBuff = sl::Span(reinterpret_cast<char*>(&hpet), sizeof(hpet));
        if (CopyAcpiTable(SigHpet, hpetBuff) == 0)
        {
            Log("HPET not available on this system", LogLevel::Info);
            return;
        }

        hpetRegs = hpetMmioBase;
        MmuMap(LocalDomain().kernelSpace, hpetRegs.ptr, hpet.baseAddress.address, 
            MmuFlag::Mmio | MmuFlag::Write);

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
            hpetPeriod / FemtosPerNano, hpetRegs.ptr, hpet.baseAddress.address);
    }

    static inline uint16_t PitRead()
    {
        Out8(PortPitCmd, 0); //latch channel 0
        uint16_t data = In8(PortPitData);
        data |= (uint16_t)In8(PortPitData) << 8;
        return data;
    }

    TimerNanos CalibrationSleep(TimerNanos nanos)
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
}
