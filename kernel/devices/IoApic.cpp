#include <devices/IoApic.h>
#include <acpi/AcpiTables.h>
#include <memory/Paging.h>
#include <Algorithms.h>
#include <Memory.h>
#include <Log.h>

namespace Kernel::Devices
{
    constexpr const char* TriggerModeStrs[] =
    { "edge", "level" };

    constexpr const char* PinPolarityStrs[] = 
    { "active-high", "active-low"};
    
    void IoApicRedirectEntry::SetNmi(IoApicEntryModifier nmi)
    {
        uint64_t destApicId = 0;

        //vector info is ignored, but make sure we set it for nmi delivery mode
        raw = (destApicId << 56) | (((uint64_t)nmi.triggerMode & 0b1) << 15) | (((uint64_t)nmi.polarity & 0b1) << 13) | (0b100 << 8);
    }
    
    void IoApicRedirectEntry::Set(uint8_t destApicId, uint8_t vector, IoApicTriggerMode triggerMode, IoApicPinPolarity pinPolarity)
    {
        //setting bit 16 as well, to mask by default.
        raw = ((uint64_t)destApicId << 56) | (1 << 16) | (((uint64_t)triggerMode & 0b1) << 15) | (((uint64_t)pinPolarity & 0b1) << 13) | vector;
    }

    void IoApicRedirectEntry::SetMask(bool masked)
    {
        raw &= ~(1 << 16);
        if (masked)
            raw |= (1 << 16);
    }

    bool IoApicRedirectEntry::IsMasked()
    {
        return (raw & (1 << 16)) != 0;
    }

    void IoApic::WriteReg(IoApicRegister reg, uint32_t value)
    {
        sl::MemWrite<uint32_t>(baseAddress.raw + IoApicRegisterSelectOffset, (uint32_t)reg);
        sl::MemWrite<uint32_t>(baseAddress.raw + IoApicRegisterWindowOffset, value);
    }

    uint32_t IoApic::ReadReg(IoApicRegister reg)
    {
        sl::MemWrite<uint32_t>(baseAddress.raw + IoApicRegisterSelectOffset, (uint32_t)reg);
        return sl::MemRead<uint32_t>(baseAddress.raw + IoApicRegisterWindowOffset);
    }


    void IoApic::Init(uint64_t address, uint8_t id, uint8_t gsiBase)
    {
        baseAddress = EnsureHigherHalfAddr(address);
        Memory::PageTableManager::Current()->MapMemory(baseAddress, address, Memory::MemoryMapFlags::AllowWrites);

        apicId = id;
        this->gsiBase = gsiBase;
        maxRedirects = (ReadReg(IoApicRegister::Version) >> 16) & 0xFF;
        maxRedirects++; //stored value is n - 1

        //ensure everything is masked by default
        for (size_t i = 0; i < maxRedirects; i++)
            SetPinMask(i, true);

        Logf("New IO APIC initialized: base=0x%lx, id=%u, gsiBase=%u, maxRedirects=%u", LogSeverity::Info, baseAddress.raw, apicId, gsiBase, maxRedirects);
    }

    //these have to be pointers so the compiler dosnt generate global ctors
    sl::Vector<IoApic>* ioApics;
    sl::Vector<IoApicEntryModifier>* modifiers;
    sl::Vector<IoApicEntryModifier>* nmis;
    void IoApic::InitAll()
    {
        ioApics = new sl::Vector<IoApic>();
        modifiers = new sl::Vector<IoApicEntryModifier>();
        nmis = new sl::Vector<IoApicEntryModifier>();
        
        using namespace ACPI;
        MADT* madt = static_cast<MADT*>(AcpiTables::Global()->Find(SdtSignature::MADT));
        if (!madt)
        {
            Log("MADT not found, cannot initialize IO APICs.", LogSeverity::Error);
            return;
        }

        size_t madtEnd = (size_t)madt + madt->length;
        sl::NativePtr scan = madt->controllerEntries;
        while (scan.raw < madtEnd)
        {
            switch (scan.As<MadtEntry>()->type)
            {
            case MadtEntryType::IoApic:
                {
                    MadtEntries::IoApicEntry* realEntry = scan.As<MadtEntries::IoApicEntry>();
                    ioApics->EmplaceBack();
                    ioApics->Back().Init(realEntry->mmioAddress, realEntry->apicId, realEntry->gsiBase);
                    break;
                }
            case MadtEntryType::InterruptSourceOverride:
                {
                    MadtEntries::InterruptSourceOverrideEntry* realEntry = scan.As<MadtEntries::InterruptSourceOverrideEntry>();
                    IoApicEntryModifier& last = modifiers->EmplaceBack();
                    last.irqNum = realEntry->source;
                    last.gsiNum = realEntry->mappedSource;

                    if (((uint16_t)realEntry->flags & 0b10) != 0)
                        last.polarity = IoApicPinPolarity::ActiveLow;
                    else if (((uint16_t)realEntry->flags & 0b01) != 0)
                        last.polarity = IoApicPinPolarity::ActiveHigh;
                    else
                        last.polarity = IoApicPinPolarity::Default;

                    if (((uint16_t)realEntry->flags & 0b1000) != 0)
                        last.triggerMode = IoApicTriggerMode::Level;
                    else if (((uint16_t)realEntry->flags & 0b0100) != 0)
                        last.triggerMode = IoApicTriggerMode::Edge;
                    else
                        last.triggerMode = IoApicTriggerMode::Default;
                    
                    Logf("MADT ISO entry: irq%u -> gsi%u, polarity=%s, mode=%s", LogSeverity::Verbose,
                        last.irqNum, last.gsiNum, PinPolarityStrs[(size_t)last.polarity], TriggerModeStrs[(size_t)last.polarity]);
                    
                    break;
                }
            case MadtEntryType::NmiSource:
                {
                    MadtEntries::NmiSourceEntry* realEntry = scan.As<MadtEntries::NmiSourceEntry>();
                    IoApicEntryModifier& last = nmis->EmplaceBack();
                    last.irqNum = 0;
                    last.gsiNum = realEntry->gsiNumber;

                    if (((uint16_t)realEntry->flags & 0b10) != 0)
                        last.polarity = IoApicPinPolarity::ActiveLow;
                    else if (((uint16_t)realEntry->flags & 0b01) != 0)
                        last.polarity = IoApicPinPolarity::ActiveHigh;
                    else
                        last.polarity = IoApicPinPolarity::Default;

                    if (((uint16_t)realEntry->flags & 0b1000) != 0)
                        last.triggerMode = IoApicTriggerMode::Level;
                    else if (((uint16_t)realEntry->flags & 0b0100) != 0)
                        last.triggerMode = IoApicTriggerMode::Edge;
                    else
                        last.triggerMode = IoApicTriggerMode::Default;
                    
                    Logf("MADT NMI entry: gsi%u, polarity=%s, mode=%s", LogSeverity::Verbose,
                        last.irqNum, last.gsiNum, PinPolarityStrs[(size_t)last.polarity], TriggerModeStrs[(size_t)last.polarity]);

                    break;
                }

            default:
                break;
            }

            scan.raw += scan.As<MadtEntry>()->length;
        }

        //apply the nmis now. We have to write these manually, as they're blocked by WriteRedirectEntry()
        for (size_t i = 0; i < nmis->Size(); i++)
        {
            IoApicEntryModifier nmi = nmis->At(i);
            IoApicRedirectEntry entry;
            entry.SetNmi(nmi);

            IoApicRegister regLow = (IoApicRegister)((uint64_t)IoApicRegister::Redirect0 + nmi.gsiNum * 2);
            IoApicRegister regHigh = (IoApicRegister)((uint64_t)IoApicRegister::Redirect0 + nmi.gsiNum * 2 + 1);
            Global(nmi.gsiNum)->WriteReg(regLow, entry.raw);
            Global(nmi.gsiNum)->WriteReg(regHigh, entry.raw >> 32);
        }

        Logf("IO APIC parsing finished, nmis=%u, sourceOverrides=%u", LogSeverity::Verbose, nmis->Size(), modifiers->Size());
    }

    IoApic* IoApic::Global(size_t ownsGsi)
    {
        //NOTE: this assumes vector is ordered, otherwise this will likely return junk
        IoApic* last = ioApics->Begin();
        while (last + 1 < ioApics->End())
        {
            IoApic* next = last + 1;
            if (next->gsiBase > ownsGsi)
                break;
            last++;
        }
        return last;
    }

    IoApicEntryModifier IoApic::TranslateToGsi(uint8_t irqNumber)
    {
        uint8_t fixedIrq = irqNumber < 0x20 ? irqNumber + 0x20 : irqNumber;
        IoApicEntryModifier* mod = sl::FindIf(modifiers->Begin(), modifiers->End(), [&] (IoApicEntryModifier* mod) { return mod->irqNum == irqNumber; });

        if (mod == modifiers->End())
            return 
            { 
                .irqNum = irqNumber, 
                .gsiNum = fixedIrq, 
                .polarity = IoApicPinPolarity::Default, 
                .triggerMode = IoApicTriggerMode::Default 
            };
        
        return
        {
            .irqNum = mod->gsiNum,
            .gsiNum = fixedIrq,
            .polarity = mod->polarity,
            .triggerMode = mod->triggerMode
        };
    }

    void IoApic::SetPinMask(uint8_t pinNum, bool masked)
    {
        auto redirect = ReadRedirect(pinNum);
        if (masked)
            redirect.raw |= (1 << 16);
        else
            redirect.raw &= ~(1 << 16);
        WriteRedirect(pinNum, redirect);
    }

    void IoApic::WriteRedirect(uint8_t destApicId, IoApicEntryModifier entryMod)
    {
        IoApicRedirectEntry entry;
        entry.Set(destApicId, entryMod.gsiNum, entryMod.triggerMode, entryMod.polarity);
        WriteRedirect(entryMod.irqNum, entry);
    }

    void IoApic::WriteRedirect(uint8_t pinNum, IoApicRedirectEntry entry)
    {
        if (pinNum >= maxRedirects)
        {
            Logf("Attempted to write IO APIC redirect %u - too high.", LogSeverity::Error, pinNum);
            return;
        }

        //check we dont overwrite any nmis
        for (size_t i = 0; i < nmis->Size(); i++)
        {
            if (nmis->At(i).gsiNum == pinNum)
            {
                Logf("Attempted to overwrite NMI IO APIC redirect %u.", LogSeverity::Error, pinNum);
                return;
            }
        }

        IoApicRegister regLow = (IoApicRegister)((uint64_t)IoApicRegister::Redirect0 + pinNum * 2);
        IoApicRegister regHigh = (IoApicRegister)((uint64_t)IoApicRegister::Redirect0 + pinNum * 2 + 1);
        WriteReg(regLow, entry.raw);
        WriteReg(regHigh, entry.raw >> 32);
    }

    IoApicRedirectEntry IoApic::ReadRedirect(uint8_t pinNum)
    {
        if (pinNum >= maxRedirects)
        {
            Logf("Attempted to read IO APIC redirect %u - too high.", LogSeverity::Error, pinNum);
            return {};
        }

        IoApicRegister regLow = (IoApicRegister)((uint64_t)IoApicRegister::Redirect0 + pinNum * 2);
        IoApicRegister regHigh = (IoApicRegister)((uint64_t)IoApicRegister::Redirect0 + pinNum * 2 + 1);
        
        IoApicRedirectEntry entry;
        entry.raw = ReadReg(regLow) | ((uint64_t)ReadReg(regHigh) << 32);

        return entry;
    }
}
