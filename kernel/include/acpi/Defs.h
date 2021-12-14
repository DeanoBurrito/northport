#pragma once

#include <stdint.h>

namespace Kernel::ACPI
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
        //fixed ACPI description table
        FADT = 5,
    };

    constexpr inline const char* SdtSignatureLiterals[] = 
    {
        "APIC",
        "RSDT",
        "XSDT",
        "HPET",
        "MCFG",
        "FADT",
    };

    struct [[gnu::packed]] SdtHeader
    {
        uint8_t signature[4];
        uint32_t length; //length of entire table, including this header
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

    enum class PowerManagementProfile : uint8_t
    {
        Unspecified = 0,
        Desktop = 1,
        Mobile = 2,
        Workstation = 3,
        EnterpriseServer = 4,
        SohoServer = 5,
        AppliancePc = 6,
        PerformanceServer = 7,
        Tablet = 8,
    };

    enum class IaPcBootArchFlags : uint16_t
    {
        //if set: not everything is discoverable through acpi-based means
        LegacyDevices = (1 << 0),
        //if set means ps2 controller is supported
        Ps2ControllerAvailable = (1 << 1),
        //if set, expected vga addresses may cause machine check exceptions (or just dont use vga in general)
        VgaNotPresent = (1 << 2),
        //if set, means enabling MSIs is not good
        MsiNotSupported = (1 << 3),
        //if set, means the os shouldnt enable PCIe ASPM
        pcieAspmControls = (1 << 4),
        //if set, cmos is unavailable. Must use acpi 'control method time and alarm namespace device'
        CmosRtcNotPresent = (1 << 5),
    };

    enum class FixedFeatureFlags : uint32_t
    {
        //means 'wbinvld' instruction is supported, mandatory after acpi 1.0
        WriteBackInvalidCacheSupported = (1 << 0),
        //wbinvld flushes cache values, but does not guarentee caches are marked invalid
        WriteBackInvalidCacheFullFlush = (1 << 1),
        //c1 state available on all processors
        C1StateSupported = (1 << 2),
        //c2 state only works if there's a single processor
        C2OnlyUniProcessorSupported = (1 << 3),
        //cleared means button button is 'fixed feature', set means is a control method (if registered)
        PowerButtonModel = (1 << 4),
        //see power button model desc.
        SleepButtonModel = (1 << 5),
        //if set, means rtc wake status is not supported in fixed reg space
        RtcFixedRegs = (1 << 6),
        //rtc can wake system from s1-s3, if set, rtc can also wake from s4.
        RtcS4 = (1 << 7),
        //if set, timer value is 32bits, otherwise 24 bits.
        TimerValueExtension = (1 << 8),
        //if set, system supports docking.
        DockingCapacity = (1 << 9),
        //if set, means system reset can be performed by value in FADT::resetRegister
        SystemResetAvail = (1 << 10),
        //thanks microsoft/lenovo :(
        SealedCase = (1 << 11),
        //if set, system cannot detect HIDs
        Headless = (1 << 12),
        //if set, must execute a native instruction after writing to SLP_TYPx
        CpuSoftwareFlushSLP = (1 << 13),
        //if set, means bits are available to enable wake-on-pci support
        PciExtensionWake = (1 << 14),
        //if set, means acpi PM timer is trash (was it ever not?)
        UsePlatformClock = (1 << 15),
        //whether rtc sts is valid after waking from s4
        RtcStsValidAfterS4 = (1 << 16),
        //terrifying - allows acpi firmware to wake pc based on gpe wake events (anything)
        RemotePowerOnAvail = (1 << 17),
        //if set, all LAPICs must use cluster addressing when in logical mode
        ApicForceLogicalCluster = (1 << 18),
        //if set, means logical destination mode is not available
        ApicPhysicalDestModeOnly = (1 << 19),
        //affects a lot of things, check acpi manual for exact specs.
        HardwareReducedAcpi = (1 << 20),
        //means system can passively achieve better power savings in s0 than s3, and should not change system power states.
        LowPowerS0Capable = (1 << 21),
    };

    enum class ArmBootArchFlags : uint16_t
    {
        //if psci is implemented
        PsciCompliant = (1 << 0),
        //if set, hvc must be used instead of psci
        PsciUseHvc = (1 << 1),
    };

    struct [[gnu::packed]] FADT : public SdtHeader
    {
        uint32_t firmwareCtrl; //physical address of FACS, where os/firmware can chat
        uint32_t dsdtAddress;
        uint8_t reserved0;
        PowerManagementProfile preferredPmProfile;
        uint16_t smiInterruptNum; //level triggered, active low interrupt number of SCI.
        uint32_t smiCommand; //port number of the SMI command port
        uint8_t acpiEnable; //writen to smiCommand to take control of smi regs
        uint8_t acpiDisable; //written to smiCommand to return control of smi regs to firmware
        uint8_t s8BiosReg; //written to smiCommand to enter s4 state
        uint8_t pStateControl; //if non zero, value to write to smiCommand to take control of pstates
        uint32_t pm1a_eventBlock; //port address of pm1a event reg block.
        uint32_t pm1b_eventBlock; //port address of pm1b event reg block, if non-zero
        uint32_t pm1a_controlBlock; //port address of pm1a control reg block
        uint32_t pm1b_controlBlock; //port address of pm1b control reg block, if non-zero
        uint32_t pm2_controlBlock; //port address of pm2 control reg block, if non-zero
        uint32_t pm_timerBlock; //port address of acpi timer regs block
        uint32_t gpe0Block; //port address of general purpose event register block 0
        uint32_t gpe1Block;
        uint8_t pm1_eventLen; //number of bytes decoded by pm1(a|b) event blocks
        uint8_t pm1_controlLen;
        uint8_t pm2_controlLen;
        uint8_t pm_timerLen; //if supported, must be 4
        uint8_t gpe0BlockLen; //gpe0/gpe1 lengths must be multiples of 2
        uint8_t gpe1BlockLen;
        uint8_t gpe1Base; //offset within acpi events where gpe1 events begin
        uint8_t cstateChangedNotif; //value written to smiCommand to indicate support for c state changed notification
        uint16_t level2Latency; //worst case latency (microseconds) to change to/from c2 state. Greater than 100 means no c2 state.
        uint16_t level3Latency; //worrst case latency (microseconds) to change to/from c3 state. Greater than 1000 means no c3 state.
        uint16_t flushSize;
        uint16_t flushStride;
        uint8_t dutyOffset; //zero-based index of processors duty cycle setting is within P_CNT
        uint8_t dutWidth; //bit width of duty cycle setting within P_CNT
        uint8_t alarmDay;
        uint8_t alarmMon;
        uint8_t cmosCentury;
        IaPcBootArchFlags iaPcBootFlags;
        uint8_t reserved1;
        FixedFeatureFlags fixedFeatureFlags;
        GenericAddr resetRegister; //only spaces 1-3 are meaningful, must be 1 byte write
        uint8_t resetValue; //written to resetRegister
        ArmBootArchFlags armBootFlags;
        uint8_t minorVersion; //lower 4 bits = minor version, upper 4 bits = errata

        //extended region - all values starting with x_ overwrite their regular values, if non-zero
        uint64_t x_firmwareCtrl;
        uint64_t x_dsdt;
        GenericAddr x_pm1a_eventBlock;
        GenericAddr x_pm1b_eventBlock;
        GenericAddr x_pm1a_controlBlock;
        GenericAddr x_pm1b_controlBlock;
        GenericAddr x_pm2_controlBlock;
        GenericAddr x_timerBlock;
        GenericAddr x_gpe0Block;
        GenericAddr x_gpe1Block;
        GenericAddr x_sleepControlRegister; //only spaces 1-3 are meaningful, must be 1 byte write.
        GenericAddr x_sleepStatusReg; //only spaces 1-3 are meaningful, must be 1 byte write.
        uint64_t hypervisorVendorId;
    };
}
