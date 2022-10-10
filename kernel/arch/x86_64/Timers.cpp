#include <arch/x86_64/Timers.h>
#include <arch/Platform.h>
#include <memory/Vmm.h>
#include <acpi/Tables.h>
#include <debug/Log.h>
#include <NativePtr.h>

namespace Npk
{
    constexpr size_t PitPeriodNanos = 838; //838.09511ns
    
    constexpr size_t HpetRegCaps = 0x0;
    constexpr size_t HpetRegConfig = 0x10;
    constexpr size_t HpetRegCounter = 0xF0;

    constexpr size_t FemtosPerNano = 1'000'000;
    
    sl::NativePtr hpetMmio = nullptr;
    size_t hpetPeriod;

    inline uint16_t PitRead()
    {
        Out8(PortPitCmd, 0); //latch channel 0
        uint16_t data = In8(PortPitData);
        data |= (uint16_t)In8(PortPitData) << 8;
        return data;
    }

    void PitSleep(size_t nanos)
    {
        ASSERT((nanos / PitPeriodNanos) < 0xFFFF, "Sleep time too long for PIT.");
        const uint16_t target = 0xFFFF - (nanos / PitPeriodNanos);

        //reset PIT counter to max
        Out8(PortPitCmd, 0x34);
        Out8(PortPitData, 0xFF);
        Out8(PortPitData, 0xFF);

        while (PitRead() > target);
    }

    void InitTimers()
    {
        auto maybeHpet = Acpi::FindTable(Acpi::SigHpet);
        if (!maybeHpet)
            return;
        
        const Acpi::Hpet* hpetTable = static_cast<Acpi::Hpet*>(*maybeHpet);
        auto mmioRange = VMM::Kernel().Alloc(0x1000, hpetTable->baseAddress.address, VmFlags::Write | VmFlags::Mmio);
        ASSERT(mmioRange.HasValue(), "Failed to allocate hpet mmio.");
        hpetMmio = mmioRange->base;

        //reset main counter and leave it enabled
        hpetMmio.Offset(HpetRegConfig).VolatileWrite<uint64_t>(0b0);
        hpetMmio.Offset(HpetRegCounter).VolatileWrite<uint64_t>(0);
        hpetMmio.Offset(HpetRegConfig).VolatileWrite<uint64_t>(0b1); //bit 0 = enable bit
        hpetPeriod = hpetMmio.Offset(HpetRegCaps).VolatileRead<uint64_t>() >> 32;

        ASSERT(hpetPeriod <= 0x05F5E100, "Bad HPET period."); //magic number pulled from hpet spec
        ASSERT(hpetPeriod > FemtosPerNano, "HPET period < 1ns.");
        Log("Hpet available: period=%luns, regs=0x%lx (0x%lx)", LogLevel::Info, 
            hpetPeriod / FemtosPerNano, hpetMmio.raw, hpetTable->baseAddress.address);
    }

    void InitInterruptTimers()
    {
        ASSERT_UNREACHABLE(); //TODO: setup HPET comparitors, setup ioapic for hpet or pit.
    }

    void HpetSleep(size_t nanos)
    {
        ASSERT(hpetMmio.ptr != nullptr, "No HPET sleep: device is not available.");
        
        const sl::NativePtr counter = hpetMmio.Offset(HpetRegCounter);
        const size_t target = counter.VolatileRead<uint64_t>() + ((nanos * FemtosPerNano) / hpetPeriod);

        while (counter.VolatileRead<uint64_t>() < target)
            asm("pause");
    }

    const char* ActiveTimerName()
    {
        return hpetMmio.ptr == nullptr ? "pit" : "hpet";
    }

    void PolledSleep(size_t nanos)
    {
        if (hpetMmio.ptr == nullptr)
            PitSleep(nanos);
        else
            HpetSleep(nanos);
    }

    void InterruptSleep(size_t nanos, void (*callback)(size_t))
    {
        ASSERT_UNREACHABLE(); //use HPET or PIT to generate interrupts
    }

    size_t GetTimerNanos()
    {
        if (hpetMmio.ptr == nullptr)
            return (0xFFFF - PitRead()) * PitPeriodNanos;
        else
            return hpetMmio.Offset(HpetRegCounter).VolatileRead<uint64_t>() * hpetPeriod / FemtosPerNano;
    }
}
