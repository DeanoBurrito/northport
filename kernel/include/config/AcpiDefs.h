#pragma once

#include <stdint.h>
#include <stddef.h>

namespace Npk::Config
{
    enum class GenericAddrId : uint8_t
    {
        Memory = 0,
        IO = 1,
        Pci = 2,
        EC = 3,
        SMBus = 4,
        Cmos = 5,
        PciBar = 6,
        Ipmi = 7,
        Gpio = 8,
        Serial = 9
    };

    enum class GenericAddrSize : uint8_t
    {
        Undefined = 0,
        Byte = 1,
        Word = 2,
        DWord = 3,
        QWord = 4,
    };
    
    struct [[gnu::packed]] GenericAddr
    {
        GenericAddrId type;
        uint8_t bitWidth;
        uint8_t bitOffset;
        GenericAddrSize size;
        uint64_t address;
    };

    struct [[gnu::packed]] Rsdp
    {
        uint8_t signature[8]; //"RSD PTR "
        uint8_t checksum;
        uint8_t oem[6];
        uint8_t revision;
        uint32_t rsdt;
        uint32_t length;
        uint64_t xsdt;
        uint8_t checksum2;
        uint8_t reserved[3];
    };

    struct [[gnu::packed]] Sdt
    {
        uint8_t signature[4];
        uint32_t length;
        uint8_t revision;
        uint8_t checksum;
        uint8_t oem[6];
        uint8_t oemTable[8];
        uint32_t oemRevision;
        uint32_t creator;
        uint32_t creatorRevision;
    };

    constexpr size_t SdtSigLength = 4;
    constexpr const char* SigRsdt = "RSDT";
    constexpr const char* SigXsdt = "XSDT";
    constexpr const char* SigMadt = "APIC";
    constexpr const char* SigHpet = "HPET";
    constexpr const char* SigMcfg = "MCFG";

    struct [[gnu::packed]] Rsdt : public Sdt
    {
        uint32_t entries[];
    };

    struct [[gnu::packed]] Xsdt : public Sdt
    {
        uint64_t entries[];
    };

    enum class MadtFlags : uint32_t
    {
        PcAtCompat = 1 << 0,
    };
    
    enum class MadtSourceType : uint8_t
    {
        LocalApic = 0,
        IoApic = 1,
        SourceOverride = 2,
        NmiSource = 3,
        LocalApicNmi = 4,
        LocalX2Apic = 9,
        LocalX2ApicNmi = 10,
        WakeupMailbox = 16,
    };

    struct [[gnu::packed]] MadtSource
    {
        MadtSourceType type;
        uint8_t length;
    };

    namespace MadtSources
    {
        enum class LocalApicFlags : uint32_t
        {
            Enabled = 1 << 0,
            OnlineCapable = 1 << 1,
        };
        
        struct [[gnu::packed]] LocalApic : public MadtSource
        {
            uint8_t acpiProcessorId;
            uint8_t apicId;
            LocalApicFlags flags;
        };

        struct [[gnu::packed]] IoApic : public MadtSource
        {
            uint8_t apicId;
            uint8_t reserved;
            uint32_t mmioAddr;
            uint32_t gsibase;
        };

        constexpr uint16_t PolarityMask = 0b11;
        constexpr uint16_t PolarityDefault = 0b00;
        constexpr uint16_t PolarityHigh = 0b01;
        constexpr uint16_t PolarityLow = 0b11;
        constexpr uint16_t TriggerModeMask = 0b1100;
        constexpr uint16_t TriggerModeDefault = 0b0000;
        constexpr uint16_t TriggerModeEdge = 0b0100;
        constexpr uint16_t TriggerModeLevel = 0b1100;
        
        struct [[gnu::packed]] SourceOverride : public MadtSource
        {
            uint8_t bus;
            uint8_t source; //what we thought it was
            uint32_t mappedGsi; //what it actually is
            uint16_t polarityModeFlags;
        };

        struct [[gnu::packed]] NmiSource : public MadtSource
        {
            uint16_t polarityModeFlags;
            uint32_t gsi;
        };

        struct [[gnu::packed]] LocalApicNmi : public MadtSource
        {
            uint8_t acpiProcessorId;
            uint16_t polarityModeFlags;
            uint8_t lintNumber;
        };

        struct [[gnu::packed]] LocalX2Apic : public MadtSource
        {
            uint16_t reserved;
            uint32_t apicId;
            LocalApicFlags flags;
            uint32_t acpiProcessorId;
        };

        struct [[gnu::packed]] LocalX2ApicNmi : public MadtSource
        {
            uint16_t polarityModeFlags;
            uint32_t acpiProcessorId;
            uint8_t lintNumber;
            uint8_t reserved[3];
        };

        struct [[gnu::packed]] WakeupMailbox : public MadtSource
        {
            uint16_t version;
            uint32_t reserved;
            uint64_t mailboxAddress;
        };
    }
    
    struct [[gnu::packed]] Madt : public Sdt
    {
        uint32_t controllerAddr;
        MadtFlags flags;
        MadtSource sources[];
    };

    struct [[gnu::packed]] Hpet : public Sdt
    {
        uint32_t eventTimerBlockId;
        GenericAddr baseAddress;
        uint8_t hpetNumber;
        uint16_t minClockTicks;
        uint8_t pageProtection;
    };

    struct [[gnu::packed]] McfgSegment
    {
        uint64_t base;
        uint16_t id;
        uint8_t firstBus;
        uint8_t lastBus;
        uint32_t reserved;
    };

    struct [[gnu::packed]] Mcfg : public Sdt
    {
        uint64_t reserved; //good one pci-sig, all in a day's work i'm sure.
        McfgSegment segments[];
    };
}
