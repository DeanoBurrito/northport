#include <devices/LApic.h>
#include <devices/8254Pit.h>
#include <devices/SystemClock.h>
#include <acpi/AcpiTables.h>
#include <memory/Paging.h>
#include <Platform.h>
#include <Memory.h>
#include <Utilities.h>
#include <Log.h>
#include <Locks.h>
#include <Cpu.h>

#define LAPIC_TIMER_CALIBRATION_MS 25

#define ExtractX2Offset(reg) ((((uint64_t)reg) >> 4) + 0x800)

namespace Kernel::Devices
{
    void LvtEntry::Set(uint8_t vector)
    {
        raw = vector | (1 << 16); 
    }

    void LvtEntry::Set(uint8_t vector, ApicDeliveryMode mode, bool levelTriggered)
    {
        Set(vector, mode, false, levelTriggered);
    }

    void LvtEntry::Set(uint8_t vector, ApicDeliveryMode mode, bool activeLow, bool levelTriggered)
    {
        raw = vector | ((uint64_t)mode << 8) | (activeLow ? 1 << 13 : 0) | (levelTriggered ? 1 << 15 : 0) | (1 << 16);
    }

    void LvtEntry::Set(uint8_t vector, ApicTimerMode mode)
    {
        //timer specific entry
        raw = vector | (1 << 16) | ((uint64_t)mode << 17);
    }

    void LApic::WriteReg(LocalApicRegister reg, uint32_t value) const
    {
        if(x2ModeEnabled)
        {
            CPU::WriteMsr(ExtractX2Offset(reg), value);
        }
        else
        {
            sl::MemWrite<uint32_t>(baseAddress + (uint64_t)reg, value);
        }
    }

    uint32_t LApic::ReadReg(LocalApicRegister reg) const
    {
        if(x2ModeEnabled)
        {
            return (uint32_t) CPU::ReadMsr(ExtractX2Offset(reg));
        }
        else
        {
            return sl::MemRead<uint32_t>(baseAddress + (uint64_t)reg);
        }
    }

    char lapicCalibLock;
    void LApic::CalibrateTimer()
    {
        sl::ScopedSpinlock scopeLock(&lapicCalibLock);
        
        WriteReg(LocalApicRegister::TimerInitialCount, 0); //ensure timer is stopped before we mess with it
        WriteReg(LocalApicRegister::TimerLVT, INTERRUPT_GSI_IGNORE); //everything else is set to 0
        calibratedDivisor = (uint32_t)ApicTimerDivisor::_2;
        WriteReg(LocalApicRegister::TimerDivisor, calibratedDivisor);
        
        //mask pit to clear its current tick count (it's being used for uptime calculations before this)
        SetPitMasked(true);

        //start pit and apic timer
        SetPitMasked(false);
        WriteReg(LocalApicRegister::TimerInitialCount, (uint32_t)-1); //max value, so we count down

        while (GetPitTicks() < LAPIC_TIMER_CALIBRATION_MS);

        uint32_t currentValue = ReadReg(LocalApicRegister::TimerCurrentCount);
        WriteReg(LocalApicRegister::TimerInitialCount, 0);
        SetPitMasked(true);

        timerTicksPerMs = ((uint32_t)-1) - currentValue;
        timerTicksPerMs = timerTicksPerMs / LAPIC_TIMER_CALIBRATION_MS;

        Logf("Local APIC timer calibration for %u ticks per millisecond.", LogSeverity::Verbose, timerTicksPerMs);
    }

    LApic* LApic::Local()
    {
        return GetCoreLocal()->ptrs[CoreLocalIndices::LAPIC].As<LApic>();
    }

    void LApic::Init()
    {
        if (!CPU::FeatureSupported(CpuFeature::APIC))
            Log("CPUID says APIC is not unavailable, cannot initialize local apic.", LogSeverity::Fatal);

        baseAddress = (CPU::ReadMsr(MSR_APIC_BASE) & ~(0xFFF)) + vmaHighAddr;
        bool isX2ApicFeaturePresent = CPU::FeatureSupported(CpuFeature::X2APIC);
        Memory::PageTableManager::Local()->MapMemory(baseAddress, baseAddress - vmaHighAddr, Memory::MemoryMapFlag::AllowWrites);
        
        timerTicksPerMs = 0;

        uint64_t apic_msr_base = CPU::ReadMsr(MSR_APIC_BASE);
        if ((CPU::ReadMsr(MSR_APIC_BASE) & (1 << 11)) == 0)
            Log("IA32_APIC_BASE_MSR bit 11 (global enable) is cleared. Cannot initialize local apic.", LogSeverity::Fatal);

        if(isX2ApicFeaturePresent) 
        {
            if (((apic_msr_base >> 10) & 0b11) != 0b11) 
            {
                x2ModeEnabled = isX2ApicFeaturePresent;
                //To Enable x2apic we need to set the 10th bit in the MSR register and write it back.
                apic_msr_base = apic_msr_base | (1 << 10);
                CPU::WriteMsr(MSR_APIC_BASE, apic_msr_base);
                if(IsBsp())
                    Log("System Succesfully initialized X2Apic", LogSeverity::Verbose);
            } 
            else
            {
                if(IsBsp())
                    Log("System is using X2APIC, initialized by bootloader", LogSeverity::Verbose);
            }

            apicId = ReadReg(LocalApicRegister::Id);
            x2ModeEnabled = true;
        } 
        else 
        {
            if(IsBsp())
                Log("Local Apic is running in XAPIC Mode", LogSeverity::Verbose);
            apicId = ReadReg(LocalApicRegister::Id) >> 24;
        }

        ACPI::MADT* madt = reinterpret_cast<ACPI::MADT*>(ACPI::AcpiTables::Global()->Find(ACPI::SdtSignature::MADT));
        if (madt == nullptr)
            Log("APIC unable to get madt data, does it exist? Cannot check for existence of 8259 PICs.", LogSeverity::Error);
        else if (IsBsp() && sl::EnumHasFlag(madt->flags, ACPI::MadtFlags::Dual8259sInstalled))
        {
            Log("BSP Local APIC is disabling dual 8259 PICs.", LogSeverity::Verbose);

            //remap pics above native interrupts range, then mask them
            CPU::PortWrite8(0x20, 0x11); //start standard init sequence (in cascade mode)
            CPU::PortWrite8(0xA0, 0x11);

            CPU::PortWrite8(0x21, 0x20); //setting pic offsets (in case they mis-trigger by accident)
            CPU::PortWrite8(0xA1, 0x28);

            CPU::PortWrite8(0x21, 0x4); //setting slave yes/no and ids
            CPU::PortWrite8(0xA1, 0x2);

            CPU::PortWrite8(0x21, 0x1); //setting mode (8086)
            CPU::PortWrite8(0xA1, 0x1);

            CPU::PortWrite8(0x21, 0xFF); //masking all interrupts
            CPU::PortWrite8(0xA1, 0xFF);
        }

        //make sure software enable flag is set, and a spurrious vector is set
        WriteReg(LocalApicRegister::SpuriousInterruptVector, INTERRUPT_GSI_SPURIOUS | (1 << 8));
    }

    void LApic::SendEOI() const
    {
        WriteReg(LocalApicRegister::EOI, 0);
    }

    bool LApic::IsBsp() const
    {
        return (CPU::ReadMsr(MSR_APIC_BASE) & (1 << 8)) != 0;
    }

    size_t LApic::GetId() const
    { 
        if(x2ModeEnabled) {
            return ReadReg(LocalApicRegister::Id);
        } 
        return ReadReg(LocalApicRegister::Id) >> 24; 
    }

    void LApic::SendIpi(uint32_t destId, uint8_t vector)
    {
        uint32_t low = vector;
        uint32_t high = (x2ModeEnabled ? destId : destId << 24);

        //everything else is fine as default here, all zeros.
        if(!x2ModeEnabled)
        {
            WriteReg(LocalApicRegister::ICR1, high);
            //writing to low dword sends IPI
            WriteReg(LocalApicRegister::ICR0, low);
        } 
        else
        {
            uint64_t x2IcrRegisterValue = ((uint64_t) high << 32 | (uint32_t) low);
            WriteReg(LocalApicRegister::ICR0, x2IcrRegisterValue);
        }
    }

    void LApic::BroadcastIpi(uint8_t vector, bool includeSelf)
    {
        uint32_t low = vector | (includeSelf ? (0b10 << 18) : (0b11 << 18));
        if(!x2ModeEnabled) {
            WriteReg(LocalApicRegister::ICR1, 0); //destination is ignored when using shorthand
            //writing to low dword sends IPI
            WriteReg(LocalApicRegister::ICR0, low);
        }
        else
        {
            uint64_t x2IcrRegisterValue = ((uint64_t) 0 << 32| (uint32_t) low);
            WriteReg(LocalApicRegister::ICR0, x2IcrRegisterValue);
        }
    }

    void LApic::SetLvtMasked(LocalApicRegister lvtReg, bool masked) const
    {
        uint32_t current = ReadReg(lvtReg);
        current &= ~(1 << 16); //clear it, and then reset it if needed
        if (masked)
            current |= (1 << 16);
        WriteReg(lvtReg, current);
    }

    bool LApic::GetLvtMasked(LocalApicRegister lvtReg) const
    {
        return (ReadReg(lvtReg) & (1 << 16)) != 0;
    }

    void LApic::SetupTimer(size_t millis, uint8_t vector, bool periodic)
    {
        if (timerTicksPerMs == 0)
            CalibrateTimer();
        if (timerTicksPerMs == (uint64_t)-1)
        {
            Log("Local APIC timer calibration error. Timer not available.", LogSeverity::Error);
            return;
        }

        LvtEntry timerLvt;
        timerLvt.Set(vector, periodic ? ApicTimerMode::Periodic : ApicTimerMode::OneShot);
        
        WriteReg(LocalApicRegister::TimerLVT, timerLvt.raw);
        WriteReg(LocalApicRegister::TimerDivisor, calibratedDivisor);
        SetLvtMasked(LocalApicRegister::TimerLVT, false);

        //write to initial count to start timer
        WriteReg(LocalApicRegister::TimerInitialCount, timerTicksPerMs * millis);

        //stop using PIT for uptime if apic is taking over
        if (IsBsp())
            SetApicForUptime(true);
    }

    uint64_t LApic::GetTimerIntervalMS() const
    {
        return ReadReg(LocalApicRegister::TimerInitialCount) / timerTicksPerMs;
    }
}
