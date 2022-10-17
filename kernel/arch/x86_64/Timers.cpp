#include <arch/x86_64/Timers.h>
#include <arch/x86_64/Apic.h>
#include <arch/Platform.h>
#include <config/AcpiTables.h>
#include <debug/Log.h>
#include <memory/Vmm.h>
#include <interrupts/InterruptManager.h>

namespace Npk
{
    constexpr size_t PitPeriodNanos = 838; //838.09511ns
    
    constexpr size_t HpetRegCaps = 0x0;
    constexpr size_t HpetRegConfig = 0x10;
    constexpr size_t HpetRegCounter = 0xF0;
    constexpr size_t HpetRegT0Config = 0x100;
    constexpr size_t HpetRegT0Comparator = 0x108;

    constexpr size_t FemtosPerNano = 1'000'000;
    
    sl::NativePtr hpetMmio = nullptr;
    size_t hpetPeriod;
    size_t timerVector;
    uint8_t timerIoapicPin;

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
        auto maybeHpet = Config::FindAcpiTable(Config::SigHpet);
        if (!maybeHpet)
            return;
        
        const Config::Hpet* hpetTable = static_cast<Config::Hpet*>(*maybeHpet);
        auto mmioRange = VMM::Kernel().Alloc(0x1000, hpetTable->baseAddress.address, VmFlags::Write | VmFlags::Mmio);
        ASSERT(mmioRange.HasValue(), "Failed to allocate hpet mmio.");
        hpetMmio = mmioRange->base;

        //reset main counter and leave it enabled
        hpetMmio.Offset(HpetRegConfig).VolatileWrite<uint64_t>(0);
        hpetMmio.Offset(HpetRegCounter).VolatileWrite<uint64_t>(0);
        hpetMmio.Offset(HpetRegConfig).VolatileWrite<uint64_t>(0b01); //bit 0 = enable bit, bit 1 = legacy routing
        hpetPeriod = hpetMmio.Offset(HpetRegCaps).VolatileRead<uint64_t>() >> 32;

        ASSERT(hpetPeriod <= 0x05F5E100, "Bad HPET period."); //magic number pulled from hpet spec
        ASSERT(hpetPeriod > FemtosPerNano, "HPET period < 1ns.");
        Log("Hpet available: period=%luns, regs=0x%lx (0x%lx)", LogLevel::Info, 
            hpetPeriod / FemtosPerNano, hpetMmio.raw, hpetTable->baseAddress.address);
    }

    void InitInterruptTimers()
    {
        timerIoapicPin = 0;
        timerVector = *Interrupts::InterruptManager::Global().Alloc();

        if (hpetMmio.ptr == nullptr)
        {
            IoApic::Route(timerIoapicPin, timerVector, CoreLocal().id, TriggerMode::Edge, PinPolarity::High, true);
            Log("Pit selected as system timer, will use vector 0x%lx (via ioapic pin %u", LogLevel::Info, timerVector, timerIoapicPin);
        }
        else
        {
            //NOTE: we skip a lot of HPET init stuff because we only use comparator 0, which
            //always exists, according to spec.
            uint64_t configWord = hpetMmio.Offset(HpetRegT0Config).VolatileRead<uint64_t>();
            uint32_t allowedRoutes = configWord >> 32;
            while ((allowedRoutes & 1) == 0)
            {
                timerIoapicPin++;
                allowedRoutes >>= 1;
            }

            configWord &= ~(0b1111ul << 9); //zero routing field before writing
            configWord |= timerIoapicPin << 9;
            configWord &= ~(1ul << 3); //clear type bit = one shot timer
            configWord |= 1ul << 2; //enable interrupts
            configWord &= ~(1ul << 1); //edge triggered
            hpetMmio.Offset(HpetRegT0Config).VolatileWrite(configWord);
            ASSERT(hpetMmio.Offset(HpetRegT0Config).VolatileRead<uint64_t>() == configWord, "HPET comparator 0 readback incorrect.");

            IoApic::Route(timerIoapicPin, timerVector, CoreLocal().id, TriggerMode::Edge, PinPolarity::High, true);
            Log("Hpet selected as system timer, will use vector 0x%lx (via ioapic pin %u)", LogLevel::Info, timerVector, timerIoapicPin);
        }
    }

    void HpetSleep(size_t nanos)
    {
        ASSERT(hpetMmio.ptr != nullptr, "No HPET sleep: device is not available.");
        
        const sl::NativePtr counter = hpetMmio.Offset(HpetRegCounter);
        const size_t target = counter.VolatileRead<uint64_t>() + ((nanos * FemtosPerNano) / hpetPeriod);

        while (counter.VolatileRead<uint64_t>() < target)
            asm("pause");
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
        InterruptGuard intGuard;

        if (callback != nullptr)
        {
            Interrupts::InterruptManager::Global().Detach(timerVector);
            Interrupts::InterruptManager::Global().Attach(timerVector, callback);
        }
        
        if (hpetMmio.ptr == nullptr)
        {
            ASSERT((nanos / PitPeriodNanos) < 0xFFFF, "Sleep time too long for PIT.");

            const uint16_t target = nanos / PitPeriodNanos;
            //mode 0, sets output high (level + high pintrigger) when count reached.
            Out8(PortPitCmd, 0x30); //mode 0, one-shot edge triggered (active high) timer.
            Out8(PortPitData, target);
            Out8(PortPitData, target >> 8);
        }
        else
        {
            const uint64_t mainCounter = hpetMmio.Offset(HpetRegCounter).VolatileRead<uint64_t>();
            const uint64_t target = mainCounter + ((nanos * FemtosPerNano) / hpetPeriod);
            hpetMmio.Offset(HpetRegT0Comparator).VolatileWrite(target);
        }
        IoApic::Mask(timerIoapicPin, false);
    }

    size_t GetTimerNanos()
    {
        if (hpetMmio.ptr == nullptr)
            return (0xFFFF - PitRead()) * PitPeriodNanos;
        else
            return hpetMmio.Offset(HpetRegCounter).VolatileRead<uint64_t>() * hpetPeriod / FemtosPerNano;
    }

    const char* ActiveTimerName()
    {
        return hpetMmio.ptr == nullptr ? "pit" : "hpet";
    }
}
