#pragma once

#include <stdint.h>
#include <stddef.h>

namespace Npk::Services
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
    constexpr const char SigRsdt[] = "RSDT";
    constexpr const char SigXsdt[] = "XSDT";
    constexpr const char SigMadt[] = "APIC";
    constexpr const char SigHpet[] = "HPET";
    constexpr const char SigMcfg[] = "MCFG";
    constexpr const char SigSrat[] = "SRAT";
    constexpr const char SigRhct[] = "RHCT";

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
        RvLocalController = 24,
        Imsic = 25,
        Aplic = 26,
        Plic = 27,
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

        struct [[gnu::packed]] RvLocalController : public MadtSource
        {
            uint8_t version;
            uint8_t reserved;
            uint32_t flags;
            uint64_t hartId;
            uint32_t acpiProcessorId;
            uint32_t controllerId;
            uint64_t imsicMmioBase;
            uint32_t imsicMmioLength;
        };

        struct [[gnu::packed]] Imsic : public MadtSource
        {
            uint8_t version;
            uint8_t reserved;
            uint32_t flags;
            uint16_t smodeIdents;
            uint16_t guestIdents;
            uint8_t guestIndexBits;
            uint8_t hartIndexBits;
            uint8_t groupIndexBits;
            uint8_t groupIndexShift;
        };

        struct [[gnu::packed]] Aplic : public MadtSource
        {
            uint8_t version;
            uint8_t id;
            uint32_t flags;
            uint64_t hardwareId;
            uint16_t idcCount;
            uint16_t sourcesCount;
            uint32_t gsiBase;
            uint64_t mmioBase;
            uint32_t mmioLength;
        };

        struct [[gnu::packed]] Plic : public MadtSource
        {
            uint8_t version;
            uint8_t id;
            uint64_t hardwareId;
            uint16_t sourcesCount;
            uint16_t maxPriority;
            uint32_t flags;
            uint32_t mmioLength;
            uint64_t mmioBase;
            uint32_t gsiBase;
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

    enum class SrasType : uint8_t
    {
        LocalApic = 0,
        Memory = 1,
        X2Apic = 2,
        Generic = 5,
    };

    struct [[gnu::packed]] Sras
    {
        SrasType type;
        uint8_t length;
    };

    namespace SratStructs
    {
        struct [[gnu::packed]] LocalApicSras : public Sras
        {
            uint8_t domain0;
            uint8_t apicId;
            uint32_t flags; //bit 0 = online
            uint8_t sapicEid;
            uint8_t domain1[3];
            uint32_t clockDomain;
        };

        enum class MemorySrasFlags : uint32_t
        {
            Enabled = 1 << 0,
            HotPluggable = 1 << 1,
            NonVolatile = 1 << 2,
        };

        struct [[gnu::packed]] MemorySras : public Sras
        {
            uint32_t domain;
            uint16_t reserved0;
            uint32_t baseLow;
            uint32_t baseHigh;
            uint32_t lengthLow;
            uint32_t lengthHigh;
            uint32_t reserved1;
            MemorySrasFlags flags;
        };

        struct [[gnu::packed]] X2ApicSras : public Sras
        {
            uint16_t reserved;
            uint32_t domain;
            uint32_t apicId;
            uint32_t flags; //same as LocalApicSras flags
            uint32_t clockDomain;
        };

        struct [[gnu::packed]] GenericSras : public Sras
        {
            uint8_t reserved0;
            uint8_t handleType;
            uint32_t domain;
            uint8_t handle[16];
            uint32_t flags;
            uint32_t reserved1;
            uint64_t acpiHid;
            uint32_t acpiUid;
        };
    }

    struct [[gnu::packed]] Srat : public Sdt
    {
        uint32_t reserved0;
        uint64_t reserved1;
        Sras resStructs[];
    };

    enum class RhctFlags : uint32_t
    {
        TimerCannotWake = 1 << 0,
    };

    struct [[gnu::packed]] Rhct : public Sdt
    {
        RhctFlags flags;
        uint64_t timebaseFrequency;
        uint32_t nodeCount;
        uint32_t nodesOffset;
    };

    enum class RhctNodeType : uint16_t
    {
        IsaString = 0,
        Cmo = 1,
        Mmu = 2,
        HartInfo = 65535
    };

    enum class MmuNodeType : uint8_t
    {
        Sv39 = 0,
        Sv48 = 1,
        Sv57 = 2,
    };

    struct [[gnu::packed]] RhctNode
    {
        RhctNodeType type;
        uint16_t length;
        uint16_t revision;
    };

    namespace RhctNodes
    {
        struct [[gnu::packed]] IsaStringNode : public RhctNode
        {
            uint16_t strLength;
            uint8_t str[];
        };

        struct [[gnu::packed]] CmoNode : public RhctNode
        {
            uint8_t reserved;
            uint8_t cbomSize;
            uint8_t cbopSize;
            uint8_t cbozSize;
        };

        struct [[gnu::packed]] MmuNode : public RhctNode
        {
            uint8_t reserved;
            MmuNodeType type;
        };

        struct [[gnu::packed]] HartInfoNode : public RhctNode
        {
            uint16_t offsetCount;
            uint32_t acpiProcessorId;
            uint32_t offsets[]; //offsets to linked structures, relative to start of RHCT
        };
    }
}
