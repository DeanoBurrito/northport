#include <arch/x86_64/Apic.h>
#include <arch/x86_64/Timers.h>
#include <arch/Cpu.h>
#include <acpi/Tables.h>
#include <debug/Log.h>
#include <memory/Vmm.h>
#include <interrupts/InterruptManager.h>
#include <containers/Vector.h>
#include <UnitConverter.h>

namespace Npk
{
    /*
        This version of northport is actually a rewrite of the original. A big chunk of
        the APIC code (especially the x2apic stuff) was written by Ivan (github.com/dreamos82).
        The original commits aren't in this branch, but his contributions are still here.
    */
    constexpr inline uint32_t X2Reg(LApicReg reg)
    { return ((uint32_t)reg >> 4) + 0x800; }

    constexpr uint32_t TimerDivisor = 0;
    
    uint32_t LocalApic::ReadReg(LApicReg reg) const
    {
        if (inX2mode)
            return ReadMsr(X2Reg(reg));
        else
            return mmio.Offset((size_t)reg).VolatileRead<uint32_t>();
    }

    void LocalApic::WriteReg(LApicReg reg, uint32_t value) const
    {
        if (inX2mode)
            WriteMsr(X2Reg(reg), value); //Note: this breaks for ICR, as it's the exception to the rule here.
        else
            mmio.Offset((size_t)reg).VolatileWrite(value);
    }

    inline uint64_t LocalApic::ReadTsc() const
    {
        uint64_t lo, hi;
        asm volatile("lfence; rdtsc" : "=a"(lo), "=d"(hi) :: "memory");
        return lo | (hi << 32);
    }

    size_t LocalApic::timerVector;
    
    LocalApic& LocalApic::Local()
    { return *reinterpret_cast<LocalApic*>(CoreLocal().interruptControl); }

    void LocalApic::Init()
    {
        sl::ScopedLock scopeLock(lock);
        ASSERT(CpuHasFeature(CpuFeature::Apic), "No local APIC detected.");
        
        const uint64_t msrBase = ReadMsr(MsrApicBase);
        ASSERT((msrBase & (1 << 11)) != 0, "Local APIC globally disabled.");

        if (CpuHasFeature(CpuFeature::ApicX2))
        {
            inX2mode = true;
            bool kernelSetup = true;
            if (msrBase & (1 << 10))
                kernelSetup = false;
            else
                WriteMsr(MsrApicBase, msrBase | (1 << 10));
            
            Log("Local apic in x2 mode, setup by %s.", LogLevel::Verbose, kernelSetup ? "kernel" : "bootloader");

            id = ReadReg(LApicReg::Id);
        }
        else
        {
            inX2mode = false;
            auto mmioRange = VMM::Kernel().Alloc(0x1000, msrBase & ~0xFFFul, VmFlags::Write | VmFlags::Mmio);
            ASSERT(mmioRange.HasValue(), "Failed to allocate range for APIC mmio.");
            mmio = mmioRange->base;
            Log("Local apic setup by kernel, regs=0x%lx (0x%lx)", LogLevel::Verbose, mmio.raw, msrBase & ~0xFFFul);
            id = ReadReg(LApicReg::Id) >> 24;
        }

        auto maybeMadt = Acpi::FindTable(Acpi::SigMadt);
        Acpi::Madt* madt = static_cast<Acpi::Madt*>(*maybeMadt);
        const bool picsPresent = maybeMadt.HasValue() ? true : (uint32_t)madt->flags & (uint32_t)Acpi::MadtFlags::PcAtCompat;
        if (IsBsp() && picsPresent)
        {
            Log("Disabling legacy 8259 PICs.", LogLevel::Info);

            constexpr uint16_t PortCmd0 = 0x20;
            constexpr uint16_t PortCmd1 = 0xA0;
            constexpr uint16_t PortData0 = 0x21;
            constexpr uint16_t PortData1 = 0xA1;

            //start init sequence, with 4 command words.
            Out8(PortCmd0, 0x11);
            Out8(PortCmd1, 0x11);
            //set interrupt offsets
            Out8(PortData0, 0x20);
            Out8(PortData1, 0x28);
            //set ids and master/slave status
            Out8(PortData0, 4);
            Out8(PortData1, 2);
            //set mode (8086)
            Out8(PortData0, 1);
            Out8(PortData1, 1);
            //mask all interrupts
            Out8(PortData0, 0xFF);
            Out8(PortData1, 0xFF);
        }
        (void)madt; //this pointer may or not be valid. This'll emit a warning if we accidentally use it later.

        if (IsBsp())
        {
            timerVector = *Interrupts::InterruptManager::Global().Alloc();
            Log("Local apics will use int vector 0x%lx for timers.", LogLevel::Verbose, timerVector);
        }

        WriteReg(LApicReg::SpuriousVector, 0xFF | (1 << 8));

        //select our timer source and calibrate it
        if (CpuHasFeature(CpuFeature::Tsc) && CpuHasFeature(CpuFeature::TscDeadline))
        {
            tscForTimer = true;
            CalibrateTsc();
        }
        else
        {
            tscForTimer = false;
            CalibrateTimer();
        }
    }

    void LocalApic::CalibrateTimer()
    {
        if (!CpuHasFeature(CpuFeature::AlwaysRunningApic))
            Log("Always running apic timer not supported on this cpu.", LogLevel::Warning);
        
        //ensure timer isnt running while being calibrated
        WriteReg(LApicReg::LvtTimer, 1 << 16);
        WriteReg(LApicReg::TimerInitCount, 0);
        WriteReg(LApicReg::TimerDivisor, TimerDivisor);

        ticksPerMs = 0;
        constexpr size_t calibrateCycles = 4;
        constexpr size_t calibrateMillis = 10;

        for (size_t i = 0; i < calibrateCycles; i++)
        {
            WriteReg(LApicReg::LvtTimer, 1 << 16);
            WriteReg(LApicReg::TimerInitCount, (uint32_t)-1);
            PolledSleep(calibrateMillis * 1'000'000);
            
            const size_t result = ((uint32_t)-1 - ReadReg(LApicReg::TimerCount)) / calibrateMillis;
            ticksPerMs += result;
            Log("Local apic timer calibration run: %lu ticks/ms.", LogLevel::Verbose, result);
        }
        WriteReg(LApicReg::LvtTimer, 1 << 16);
        WriteReg(LApicReg::TimerInitCount, 0);

        ticksPerMs /= calibrateCycles;
        const sl::UnitConversion freqs = sl::ConvertUnits(ticksPerMs * 1000);
        Log("Local apic timer calibrated using %s for %lu ticks/ms (%lu.%lu%shz).", LogLevel::Info, ActiveTimerName(), 
            ticksPerMs, freqs.major, freqs.minor, freqs.prefix);
    }

    void LocalApic::CalibrateTsc()
    {
        if (!CpuHasFeature(CpuFeature::InvariantTsc))
            Log("Invariant TSC not supported.", LogLevel::Warning);

        ticksPerMs = 0;
        constexpr size_t calibrateCycles = 4;
        constexpr size_t calibrateMillis = 10;

        for (size_t i = 0; i < calibrateCycles; i++)
        {
            const size_t start = ReadTsc();
            PolledSleep(calibrateMillis * 1'000'000);
            const size_t end = ReadTsc();

            const size_t result = (end - start) / calibrateMillis;
            ticksPerMs += result;
            Log("Tsc calibration run: %lu ticks/ms.", LogLevel::Verbose, result);
        }
        ticksPerMs /= calibrateCycles;

        const sl::UnitConversion freqs = sl::ConvertUnits(ticksPerMs * 1000);
        Log("Local apic timer will use TSC deadline, calibrated using %s for %lu ticks/ms (%lu.%lu%shz).", LogLevel::Info,
        ActiveTimerName(), ticksPerMs, freqs.major, freqs.minor, freqs.prefix);
    }

    void LocalApic::SetTimer(size_t nanoseconds, void (*callback)(size_t))
    {
        InterruptGuard intGuard;
        sl::ScopedLock scopeLock(lock);

        //stop timer if its already running, and re-attach callback (maybe be different)
        WriteReg(LApicReg::TimerInitCount, 0);
        if (callback != nullptr)
        {
            Interrupts::InterruptManager::Global().Detach(timerVector);
            Interrupts::InterruptManager::Global().Attach(timerVector, callback);
        }

        const uint32_t ticks = (nanoseconds / 1'000'000) * ticksPerMs;
        
        if (tscForTimer)
        {
            WriteReg(LApicReg::LvtTimer, timerVector | (1 << 18));
            WriteMsr(MsrTscDeadline, ReadMsr(MsrTsc) + ticks);
        }
        else
        {
            //we want all other bits of the timer LVT as zeroes, so this is fine.
            WriteReg(LApicReg::LvtTimer, timerVector);
            WriteReg(LApicReg::TimerDivisor, TimerDivisor);
            WriteReg(LApicReg::TimerInitCount, ticks);
        }
    }

    void LocalApic::SendEoi() const
    {
        WriteReg(LApicReg::EOI, 0);
    }

    struct ApicSourceOverride
    {
        size_t incoming;
        size_t outgoing;
        PinPolarity polarity;
        TriggerMode mode;
    };
    
    sl::Vector<IoApic> ioapics;
    sl::Vector<ApicSourceOverride> sourceOverrides;

    uint32_t IoApic::ReadReg(IoApicReg reg) const
    {
        mmio.VolatileWrite((uint32_t)reg);
        return mmio.Offset(0x10).VolatileRead<uint32_t>();
    }
    
    void IoApic::WriteReg(IoApicReg reg, uint32_t value) const
    {
        mmio.VolatileWrite((uint32_t)reg);
        mmio.Offset(0x10).VolatileWrite(value);
    }

    uint64_t IoApic::ReadRedirect(size_t index) const
    {
        uint64_t value = 0;
        mmio.VolatileWrite(index * 2 + (uint32_t)IoApicReg::TableBase);
        value |= mmio.Offset(0x10).VolatileRead<uint32_t>();
        mmio.VolatileWrite(index * 2 + (uint32_t)IoApicReg::TableBase + 1);
        value |= (uint64_t)mmio.Offset(0x10).VolatileRead<uint32_t>() << 32;
        return value;
    }

    void IoApic::WriteRedirect(size_t index, uint64_t value) const
    {
        mmio.VolatileWrite(index * 2 + (uint32_t)IoApicReg::TableBase);
        mmio.Offset(0x10).VolatileWrite<uint32_t>(value);
        mmio.VolatileWrite(index * 2 + (uint32_t)IoApicReg::TableBase + 1);
        mmio.Offset(0x10).VolatileWrite<uint32_t>(value >> 32);
    }

    void IoApic::InitAll()
    {
        auto maybeMadt = Acpi::FindTable(Acpi::SigMadt);
        ASSERT(maybeMadt, "MADT not found: cannot initialize IO APIC.");
        Acpi::Madt* madt = static_cast<Acpi::Madt*>(*maybeMadt);

        sl::NativePtr scan = madt->sources;
        while (scan.raw < (uintptr_t)madt + madt->length)
        {
            const Acpi::MadtSource* sourceBase = scan.As<Acpi::MadtSource>();
            using Acpi::MadtSourceType;

            switch (sourceBase->type)
            {
            case MadtSourceType::IoApic:
            {
                const Acpi::MadtSources::IoApic* source = scan.As<Acpi::MadtSources::IoApic>();
                IoApic& apic = ioapics.EmplaceBack();

                auto mmioRange = VMM::Kernel().Alloc(PageSize, source->mmioAddr, VmFlags::Write | VmFlags::Mmio);
                ASSERT(mmioRange.HasValue(), "Failed to allocate range for IOAPIC MMIO.")
                apic.mmio = mmioRange->base;
                apic.gsiBase = source->gsibase;

                //determine the number of inputs the ioapic has
                apic.inputCount = (apic.ReadReg(IoApicReg::Version) >> 16) & 0xFF;
                apic.inputCount++;

                Log("Ioapic found: id=%u, gsiBase=%lu, inputs=%lu, regs=0x%lx (0x%x)", LogLevel::Info,
                    source->apicId, apic.gsiBase, apic.inputCount, apic.mmio.raw, source->mmioAddr);
                break;
            }

            case MadtSourceType::SourceOverride:
            {
                using namespace Acpi::MadtSources;
                const SourceOverride* source = scan.As<SourceOverride>();
                const uint16_t polarity = source->polarityModeFlags & PolarityMask;
                const uint16_t mode = source->polarityModeFlags & TriggerModeMask;
                ApicSourceOverride& so = sourceOverrides.EmplaceBack();
                
                so.incoming = source->source;
                so.outgoing = source->mappedGsi;
                so.polarity = (polarity == PolarityLow ? PinPolarity::Low : PinPolarity::High);
                so.mode = (mode == TriggerModeLevel ? TriggerMode::Level : TriggerMode::Edge);

                Log("Madt source override: %lu -> %lu, polarity=%s, triggerMode=%s", LogLevel::Verbose,
                    so.incoming, so.outgoing, so.polarity == PinPolarity::Low ? "low" : "high", so.mode == TriggerMode::Edge ? "edge" : "level");
                break;
            }

            default: 
                break;
            }

            scan.raw += sourceBase->length;
        }

        ASSERT(ioapics.Size() > 0, "No IOAPICS found.");
    }

    bool IoApic::Route(uint8_t irqNum, uint8_t destVector, size_t destCpu, TriggerMode mode, PinPolarity pol, bool masked)
    {
        const ApicSourceOverride* sourceOverride = nullptr;
        for (auto it = sourceOverrides.Begin(); it != sourceOverrides.End(); ++it)
        {
            if (it->incoming == irqNum)
            {
                sourceOverride = it;
                irqNum = sourceOverride->outgoing;
                break;
            }
        }
        
        for (auto it = ioapics.Begin(); it != ioapics.End(); ++it)
        {
            if (irqNum < it->gsiBase || irqNum >= it->gsiBase + it->inputCount)
                continue;
            
            sl::ScopedLock scopeLock(it->lock);

            uint64_t entry = destVector;
            if (masked)
                entry |= 1 << 16;
            entry |= destCpu << 56;
            entry |= (uint64_t)(sourceOverride == nullptr ? pol : sourceOverride->polarity) << 13;
            entry |= (uint64_t)(sourceOverride == nullptr ? mode : sourceOverride->mode) << 15;

            it->WriteRedirect(irqNum - it->gsiBase, entry);
            ASSERT(entry == it->ReadRedirect(irqNum - it->gsiBase), "Failed to write IOAPIC redirect entry.");

            return true;
        }

        return false;
    }

    bool IoApic::Masked(uint8_t irqNum)
    {
        for (auto it = ioapics.Begin(); it != ioapics.End(); ++it)
        {
            if (irqNum < it->gsiBase || irqNum >= it->gsiBase + it->inputCount)
                continue;
            
            sl::ScopedLock scopeLock(it->lock);
            return it->ReadRedirect(irqNum - it->gsiBase) & (1 << 16);
        }

        return true;
    }

    void IoApic::Mask(uint8_t irqNum, bool masked)
    {
        for (auto it = ioapics.Begin(); it != ioapics.End(); ++it)
        {
            if (irqNum < it->gsiBase || irqNum >= it->gsiBase + it->inputCount)
                continue;
            
            sl::ScopedLock scopeLock(it->lock);
            
            uint64_t current = it->ReadRedirect(irqNum - it->gsiBase) & (1 << 16);
            if (masked)
                current |= 1 << 16;
            else
                current &= ~(1 << 16);
            it->WriteRedirect(irqNum - it->gsiBase, current);
            return;
        }
    }
}
