#include <arch/x86_64/Apic.h>
#include <arch/Cpu.h>
#include <arch/Platform.h>
#include <arch/Timers.h>
#include <config/AcpiTables.h>
#include <debug/Log.h>
#include <memory/Vmm.h>
#include <containers/Vector.h>
#include <UnitConverter.h>
#include <Maths.h>

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
            return mmio.Offset((size_t)reg).Read<uint32_t>();
    }
    
    void LocalApic::WriteReg(LApicReg reg, uint32_t value) const
    {
        if (inX2mode)
            WriteMsr(X2Reg(reg), value); //Note: this breaks for ICR, as it's the exception to the rule here.
        else
            mmio.Offset((size_t)reg).Write(value);
    }

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

        auto maybeMadt = Config::FindAcpiTable(Config::SigMadt);
        Config::Madt* madt = static_cast<Config::Madt*>(*maybeMadt);
        const bool picsPresent = maybeMadt.HasValue() ? true : (uint32_t)madt->flags & (uint32_t)Config::MadtFlags::PcAtCompat;
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
        
        WriteReg(LApicReg::SpuriousVector, 0xFF | (1 << 8));
    }

    size_t LocalApic::MaxTimerNanos()
    {
        if (ticksPerMs < 1'000'000)
            return -1ul;
        ASSERT_UNREACHABLE();
    }

    bool LocalApic::CalibrateTimer()
    {
        if (!CpuHasFeature(CpuFeature::AlwaysRunningApic))
            Log("Always running apic timer not supported on this cpu.", LogLevel::Warning);

        WriteReg(LApicReg::LvtTimer, 1 << 16); //mask and stop timer
        WriteReg(LApicReg::TimerInitCount, 0);
        WriteReg(LApicReg::TimerDivisor, TimerDivisor);

        constexpr size_t CalibRuns = 8;
        constexpr size_t CalibMillis = 10;
        long calibTimes[CalibRuns];

        for (size_t i = 0; i < CalibRuns; i++)
        {
            WriteReg(LApicReg::LvtTimer, 1 << 16);
            WriteReg(LApicReg::TimerInitCount, (uint32_t)-1);
            PolledSleep(CalibMillis * 1'000'000);

            const uint32_t passed = (uint32_t)-1 - ReadReg(LApicReg::TimerCount);
            calibTimes[i] = (long)(passed / CalibMillis);
        }

        WriteReg(LApicReg::LvtTimer, 1 << 16);
        WriteReg(LApicReg::TimerInitCount, 0);

        auto finalTime = CoalesceTimerRuns(calibTimes, CalibRuns, 3);
        if (!finalTime)
            return false;
        
        ticksPerMs = *finalTime;

        sl::UnitConversion freqs = sl::ConvertUnits(ticksPerMs * 1000);
        Log("Lapic timer calibrated: %lu ticks/ms, %lu.%lu%shz (divisor=%u).", LogLevel::Info, ticksPerMs,
            freqs.major, freqs.minor, freqs.prefix, TimerDivisor);
        return true;
    }

    void LocalApic::SetTimer(bool tsc, size_t nanos, size_t vector)
    {
        InterruptGuard guard;
        sl::ScopedLock scopeLock(lock);

        if (tsc)
        {
            WriteReg(LApicReg::LvtTimer, vector | (1 << 18));
            WriteMsr(MsrTscDeadline, ReadMsr(MsrTsc) + nanos); //nanos is pre-calculated here, its actually a tick count.
        }
        else
        {
            WriteReg(LApicReg::LvtTimer, vector);
            WriteReg(LApicReg::TimerDivisor, TimerDivisor);
            WriteReg(LApicReg::TimerInitCount, sl::Max((nanos / 1'000'000), 1ul) * ticksPerMs);
        }
    }

    void LocalApic::SendEoi() const
    {
        WriteReg(LApicReg::EOI, 0);
    }

    void LocalApic::SendIpi(size_t dest) const
    {
        //ensure any previous IPIs have finished sending first (jic we pump this)
        while (ReadReg(LApicReg::Icr0) & (1 << 12))
            HintSpinloop();

        if (inX2mode)
            WriteMsr(0x830, (dest << 32) | IntVectorIpi);
        else
        {
            WriteReg(LApicReg::Icr1, dest << 24);
            WriteReg(LApicReg::Icr0, IntVectorIpi);
        }
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
        mmio.Write((uint32_t)reg);
        return mmio.Offset(0x10).Read<uint32_t>();
    }
    
    void IoApic::WriteReg(IoApicReg reg, uint32_t value) const
    {
        mmio.Write((uint32_t)reg);
        mmio.Offset(0x10).Write(value);
    }

    uint64_t IoApic::ReadRedirect(size_t index) const
    {
        uint64_t value = 0;
        mmio.Write(index * 2 + (uint32_t)IoApicReg::TableBase);
        value |= mmio.Offset(0x10).Read<uint32_t>();
        mmio.Write(index * 2 + (uint32_t)IoApicReg::TableBase + 1);
        value |= (uint64_t)mmio.Offset(0x10).Read<uint32_t>() << 32;
        return value;
    }

    void IoApic::WriteRedirect(size_t index, uint64_t value) const
    {
        mmio.Write(index * 2 + (uint32_t)IoApicReg::TableBase);
        mmio.Offset(0x10).Write<uint32_t>(value);
        mmio.Write(index * 2 + (uint32_t)IoApicReg::TableBase + 1);
        mmio.Offset(0x10).Write<uint32_t>(value >> 32);
    }

    void IoApic::InitAll()
    {
        auto maybeMadt = Config::FindAcpiTable(Config::SigMadt);
        ASSERT(maybeMadt, "MADT not found: cannot initialize IO APIC.");
        Config::Madt* madt = static_cast<Config::Madt*>(*maybeMadt);

        sl::NativePtr scan = madt->sources;
        while (scan.raw < (uintptr_t)madt + madt->length)
        {
            const Config::MadtSource* sourceBase = scan.As<Config::MadtSource>();
            using Config::MadtSourceType;

            switch (sourceBase->type)
            {
            case MadtSourceType::IoApic:
            {
                const Config::MadtSources::IoApic* source = scan.As<Config::MadtSources::IoApic>();
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
                using namespace Config::MadtSources;
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

    bool IoApic::Route(uint8_t& irqNum, uint8_t destVector, size_t destCpu, TriggerMode mode, PinPolarity pol, bool masked)
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
            
            uint64_t current = it->ReadRedirect(irqNum - it->gsiBase);
            if (masked)
                current |= 1 << 16;
            else
                current &= ~(1ul << 16);
            it->WriteRedirect(irqNum - it->gsiBase, current);
            return;
        }
    }
}
