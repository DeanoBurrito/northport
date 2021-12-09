#pragma once

#include <stdint.h>

namespace ACPI
{
    enum class GenericAddressSpace : uint8_t
    {
        SystemMemory = 0x0,
        SystemIO = 0x1,
        PciConfig = 0x2,
        EmbeddedController = 0x3,
        SmBus = 0x4,
        SystemCmos = 0x5,
        PciBarTarget = 0x6,
        Impi = 0x7,
        Gpio = 0x8,
        GenericSerialBus = 0x9,
        PlatformCommsChannel = 0xA,

        FixedFunctionHardware = 0x7F,
    };

    enum class GenericRegisterAccessSize : uint8_t
    {
        Undefined = 0,
        _1Byte = 1,
        _2Byte = 2,
        _4Byte = 3,
        _8Byte = 4,
    };
    
    struct [[gnu::packed]] GenericAddr
    {
        GenericAddressSpace addrSpace;
        uint8_t registerWidthBits;
        uint8_t registerOffsetBits;
        GenericRegisterAccessSize accessSize;
        uint64_t address;
    };

    struct [[gnu::packed]] RSDP
    {
        uint8_t signature[8]; //should be "RSD PTR "
        uint8_t checksum; //for the first 20 bytes of this table
        uint8_t oemId[6];
        uint8_t revision;
        uint32_t rsdtAddress;
    };

    struct [[gnu::packed]] RSDP2 : public RSDP
    {
        uint32_t length; //length of the entire table, including the full header.
        uint64_t xsdtAddress;
        uint8_t checksum2; //checksum of the entire table, including both checksum fields.
        uint8_t reserved[3];
    };

    enum class SdtSignature
    {
        //multiple apic descriptor table
        MADT = 0,
        //root system descriptor table (32bit/acpi v1 only)
        RSDT = 1,
        //extended system descriptor table (64bit/acpi v2 only)
        XSDT = 2,
        //hpet details
        HPET = 3,
        //machine config - pci mmio config space
        MCFG = 4,
    };

    constexpr inline const char* SdtSignatureLiterals[] = 
    {
        "APIC",
        "RSDT",
        "XSDT",
        "HPET",
        "MCFG",
    };

    struct [[gnu::packed]] SdtHeader
    {
        uint8_t signature[4];
        uint32_t length;
        uint8_t revision;
        uint8_t checksum;
        uint8_t oemId[6];
        uint8_t oemTableId[8];
        uint32_t oemRevision;
        uint32_t creatorId;
        uint32_t creatorRevision;
    };

    enum class MadtEntryType : uint8_t
    {
        LocalApic = 0,
        IoApic = 1,
        InterruptSourceOverride = 2,
        NmiSource = 3,
        LocalApicNmi = 4,
        LocalApicAddressOverride = 5,
        //the gap here is SAPIC entries, which are only used on itanium/IA-64.
        LocalX2Apic = 9,
        LocalX2ApicNmi = 10,
        MultiprocessorWakeup = 16,
    };

    struct [[gnu::packed]] MadtEntry
    {
        MadtEntryType type;
        uint8_t length;
    };

    namespace MadtEntries
    {
        enum class LocalApicFlags : uint32_t
        {
            Enabled = (1 << 0),
            OnlineCapable = (1 << 1),
        };

        struct [[gnu::packed]] LocalApicEntry : public MadtEntry
        {
            uint8_t acpiProcessorId; //internally consistent for acpi purposes
            uint8_t apicId; //actual hardware apic id
            LocalApicFlags flags;
        };

        struct [[gnu::packed]] IoApicEntry : public MadtEntry
        {
            uint8_t apicId;
            uint8_t reserved;
            uint32_t mmioAddress;
            uint32_t gsiBase;
        };

        enum class PolarityTriggerFlags : uint16_t
        {
            PolarityMask = 0b11,
            PolarityDefault = (0b00 << 0),
            PolarityActiveHigh = (0b01 << 0),
            PolarityActiveLow = (0b11 << 0),

            TriggerModeMask = 0b1100,
            TriggerModeDefault = (0b00 << 2),
            TriggerModeEdge = (0b01 << 2),
            TriggerModeLevel = (0b11 << 2),
        };

        struct [[gnu::packed]] InterruptSourceOverrideEntry : public MadtEntry
        {
            uint8_t alwaysZero;
            uint8_t source; //irq number
            uint32_t mappedSource; //what the source will appear as
            PolarityTriggerFlags flags;
        };

        struct [[gnu::packed]] NmiSourceEntry : public MadtEntry
        {
            PolarityTriggerFlags flags;
            uint32_t gsiNumber;
        };

        struct [[gnu::packed]] LocalApicNmiEntry : public MadtEntry
        {
            uint8_t acpiProcessorId; //for AML stuff
            PolarityTriggerFlags flags;
            uint8_t lintNumber; //LINT(n) pin that nmi is connected to
        };

        struct [[gnu::packed]] LocalApicAddressOverrideEntry : public MadtEntry
        {
            uint16_t reserved;
            uint64_t localApicAddress; //override of the entry found in MADT, which is only 32bit
        };

        struct [[gnu::packed]] LocalX2ApicEntry : public MadtEntry
        {
            uint16_t reserved;
            uint32_t apicId;
            LocalApicFlags flags;
            uint32_t acpiProcessorId;
        };

        struct [[gnu::packed]] LocalX2ApicNmiEntry : public MadtEntry
        {
            PolarityTriggerFlags flags;
            uint32_t acpiProcessorId;
            uint8_t lintNumber; //same as LAPIC NMI field
            uint8_t reserved[3];
        };

        struct [[gnu::packed]] MultiprocessorWakeupEntry : public MadtEntry
        {
            uint16_t version; //generally zero (at least in acpi spec v6.4)
            uint32_t reserved;
            uint64_t mailboxAddress; //must be in ACPINVS memory, will be 4k aligned.
        };
    }

    enum class MadtFlags : uint32_t
    {
        Dual8259sInstalled = (1 << 0),
    };

    struct [[gnu::packed]] MADT : public SdtHeader
    {
        uint32_t localApicAddress;
        MadtFlags flags;
        MadtEntry controllerEntries[];
    };

    struct [[gnu::packed]] RSDT : public SdtHeader
    {
        uint32_t entryAddresses[];
    };

    struct [[gnu::packed]] XSDT : public SdtHeader
    {
        uint64_t entryAddresses[];
    };

    enum class HpetPageProtectionFlags : uint8_t
    {
        NoProtection = 0,
        //can safely access the surrounding 3kb without issues
        _4KProtection = 1,
        //can safely access the surrounding 63kb without issues
        _64KProtection = 2,
    };

    struct [[gnu::packed]] HPET : public SdtHeader
    {
        uint32_t capabilitiesCopy; //copy of the HPET register, can ignore mostly
        GenericAddr hpetAddress; //can only appear in mmio, takes 1K of memory
        uint8_t hpetNumber;
        uint16_t minimumClockTick; //lowest safe value to set in periodic mode
        HpetPageProtectionFlags protectionAttribs;
    };

    struct [[gnu::packed]] MCFG : public SdtHeader
    {
        //TODO: implement MCFG header
    };
}
