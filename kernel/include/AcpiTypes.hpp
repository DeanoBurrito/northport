#pragma once

#include <Types.h>
#include <Compiler.h>
#include <Flags.h>

namespace Npk
{
    enum class AcpiAddrSpace : uint8_t
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

    enum class AcpiAddrSize : uint8_t
    {
        Undefined = 0,
        Byte = 1,
        Word = 2,
        DWord = 3,
        QWord = 4,
    };
    
    struct SL_PACKED(GenericAddr
    {
        AcpiAddrSpace type;
        uint8_t bitWidth;
        uint8_t bitOffset;
        AcpiAddrSize size;
        uint64_t address;
    });

    struct SL_PACKED(Rsdp
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
    });

    struct SL_PACKED(Sdt
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
    });

    constexpr size_t SdtSigLength = 4;
    constexpr const char SigRsdt[] = "RSDT";
    constexpr const char SigXsdt[] = "XSDT";
    constexpr const char SigMadt[] = "APIC";
    constexpr const char SigHpet[] = "HPET";
    constexpr const char SigMcfg[] = "MCFG";
    constexpr const char SigSrat[] = "SRAT";
    constexpr const char SigRhct[] = "RHCT";
    constexpr const char SigFadt[] = "FACP";

    struct SL_PACKED(Rsdt : public Sdt
    {
        uint32_t entries[];
    });

    struct SL_PACKED(Xsdt : public Sdt
    {
        uint64_t entries[];
    });

    enum class MadtFlag
    {
        PcAtCompat = 0,
    };

    using MadtFlags = sl::Flags<MadtFlag, uint32_t>;
    
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

    struct SL_PACKED(MadtSource
    {
        MadtSourceType type;
        uint8_t length;
    });

    namespace MadtSources
    {
        enum class LocalApicFlag
        {
            Enabled = 0,
            OnlineCapable = 1,
        };

        using LocalApicFlags = sl::Flags<LocalApicFlag, uint32_t>;
        
        struct SL_PACKED(LocalApic : public MadtSource
        {
            uint8_t acpiProcessorId;
            uint8_t apicId;
            LocalApicFlags flags;
        });

        struct SL_PACKED(IoApic : public MadtSource
        {
            uint8_t apicId;
            uint8_t reserved;
            uint32_t mmioAddr;
            uint32_t gsibase;
        });

        constexpr uint16_t PolarityMask = 0b11;
        constexpr uint16_t PolarityDefault = 0b00;
        constexpr uint16_t PolarityHigh = 0b01;
        constexpr uint16_t PolarityLow = 0b11;
        constexpr uint16_t TriggerModeMask = 0b1100;
        constexpr uint16_t TriggerModeDefault = 0b0000;
        constexpr uint16_t TriggerModeEdge = 0b0100;
        constexpr uint16_t TriggerModeLevel = 0b1100;
        
        struct SL_PACKED(SourceOverride : public MadtSource
        {
            uint8_t bus;
            uint8_t source; //what we thought it was
            uint32_t mappedGsi; //what it actually is
            uint16_t polarityModeFlags;
        });

        struct SL_PACKED(NmiSource : public MadtSource
        {
            uint16_t polarityModeFlags;
            uint32_t gsi;
        });

        struct SL_PACKED(LocalApicNmi : public MadtSource
        {
            uint8_t acpiProcessorId;
            uint16_t polarityModeFlags;
            uint8_t lintNumber;
        });

        struct SL_PACKED(LocalX2Apic : public MadtSource
        {
            uint16_t reserved;
            uint32_t apicId;
            LocalApicFlags flags;
            uint32_t acpiProcessorId;
        });

        struct SL_PACKED(LocalX2ApicNmi : public MadtSource
        {
            uint16_t polarityModeFlags;
            uint32_t acpiProcessorId;
            uint8_t lintNumber;
            uint8_t reserved[3];
        });

        struct SL_PACKED(WakeupMailbox : public MadtSource
        {
            uint16_t version;
            uint32_t reserved;
            uint64_t mailboxAddress;
        });

        struct SL_PACKED(RvLocalController : public MadtSource
        {
            uint8_t version;
            uint8_t reserved;
            uint32_t flags;
            uint64_t hartId;
            uint32_t acpiProcessorId;
            uint32_t controllerId;
            uint64_t imsicMmioBase;
            uint32_t imsicMmioLength;
        });

        struct SL_PACKED(Imsic : public MadtSource
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
        });

        struct SL_PACKED(Aplic : public MadtSource
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
        });

        struct SL_PACKED(Plic : public MadtSource
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
        });
    }
    
    struct SL_PACKED(Madt : public Sdt
    {
        uint32_t controllerAddr;
        MadtFlags flags;
        MadtSource sources[];
    });

    struct SL_PACKED(Hpet : public Sdt
    {
        uint32_t eventTimerBlockId;
        GenericAddr baseAddress;
        uint8_t hpetNumber;
        uint16_t minClockTicks;
        uint8_t pageProtection;
    });

    struct SL_PACKED(McfgSegment
    {
        uint64_t base;
        uint16_t id;
        uint8_t firstBus;
        uint8_t lastBus;
        uint32_t reserved;
    });

    struct SL_PACKED(Mcfg : public Sdt
    {
        uint64_t reserved; //good one pci-sig, all in a day's work i'm sure.
        McfgSegment segments[];
    });

    enum class SrasType : uint8_t
    {
        LocalApic = 0,
        Memory = 1,
        X2Apic = 2,
        Generic = 5,
    };

    struct SL_PACKED(Sras
    {
        SrasType type;
        uint8_t length;
    });

    namespace SratStructs
    {
        struct SL_PACKED(LocalApicSras : public Sras
        {
            uint8_t domain0;
            uint8_t apicId;
            uint32_t flags; //bit 0 = online
            uint8_t sapicEid;
            uint8_t domain1[3];
            uint32_t clockDomain;
        });

        enum class SrasFlag
        {
            Enabled = 0,
            HotPluggable = 1,
            NonVolatile = 2,
        };

        using SrasFlags = sl::Flags<SrasFlag, uint32_t>;

        struct SL_PACKED(MemorySras : public Sras
        {
            uint32_t domain;
            uint16_t reserved0;
            uint32_t baseLow;
            uint32_t baseHigh;
            uint32_t lengthLow;
            uint32_t lengthHigh;
            uint32_t reserved1;
            SrasFlags flags;
        });

        struct SL_PACKED(X2ApicSras : public Sras
        {
            uint16_t reserved;
            uint32_t domain;
            uint32_t apicId;
            uint32_t flags; //same as LocalApicSras flags
            uint32_t clockDomain;
        });

        struct SL_PACKED(GenericSras : public Sras
        {
            uint8_t reserved0;
            uint8_t handleType;
            uint32_t domain;
            uint8_t handle[16];
            uint32_t flags;
            uint32_t reserved1;
            uint64_t acpiHid;
            uint32_t acpiUid;
        });
    }

    struct SL_PACKED(Srat : public Sdt
    {
        uint32_t reserved0;
        uint64_t reserved1;
        Sras resStructs[];
    });

    enum class RhctFlag
    {
        TimerCannotWake = 0,
    };

    using RhctFlags = sl::Flags<RhctFlag, uint32_t>;

    struct SL_PACKED(Rhct : public Sdt
    {
        RhctFlags flags;
        uint64_t timebaseFrequency;
        uint32_t nodeCount;
        uint32_t nodesOffset;
    });

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

    struct SL_PACKED(RhctNode
    {
        RhctNodeType type;
        uint16_t length;
        uint16_t revision;
    });

    namespace RhctNodes
    {
        struct SL_PACKED(IsaStringNode : public RhctNode
        {
            uint16_t strLength;
            uint8_t str[];
        });

        struct SL_PACKED(CmoNode : public RhctNode
        {
            uint8_t reserved;
            uint8_t cbomSize;
            uint8_t cbopSize;
            uint8_t cbozSize;
        });

        struct SL_PACKED(MmuNode : public RhctNode
        {
            uint8_t reserved;
            MmuNodeType type;
        });

        struct SL_PACKED(HartInfoNode : public RhctNode
        {
            uint16_t offsetCount;
            uint32_t acpiProcessorId;
            uint32_t offsets[]; //offsets to linked structures, relative to start of RHCT
        });
    }

    enum class PowerProfile : uint8_t
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

    enum class FadtFlag
    {
        Wbinvd = 0,
        WbinvdFlush = 1,
        ProcC1 = 2,
        Lvl2Up = 3,
        PowerButton = 4,
        SleepButton = 5,
        FixedRtcWake = 6,
        RtcS4 = 7,
        TimerValExt = 8,
        DockingCapable = 9,
        ResetRegSupport = 10,
        SealedCase = 11,
        Headless = 12,
        CpuSwSleep = 13,
        PciWakeCapable = 14,
        UsePlatformClock = 15,
        S4RtcStatusValiv = 16,
        RemotePowerOnCapable = 17,
        ForceApicClusterModel = 18,
        ForceApicPhysicalAddressing = 19,
        HwReducedAcpi = 20,
        LowPowerS0Idle = 21,
        PersistentCpuCaches = 22,
    };

    enum class X86BootFlag
    {
        LegacyDevices = 0,
        Ps2Keyboard = 1,
        VgaNotPresent = 2,
        MsiNotSupported = 3,
        NoPcieAspmCtrls = 4,
        NoCmosRtc = 5,
    };

    enum class ArmBootFlag
    {
        PsciCompliant = 0,
        PsciUseHvc = 1,
    };

    using FadtFlags = sl::Flags<FadtFlag, uint32_t>;
    using X86BootFlags = sl::Flags<X86BootFlag, uint16_t>;
    using ArmBootFlags = sl::Flags<ArmBootFlag, uint16_t>;

    struct SL_PACKED(Fadt : public Sdt
    {
        uint32_t firmwareCtrl; //pointer to FACS table
        uint32_t dsdt;
        uint8_t reserved0;
        PowerProfile preferredPmProfile;
        uint16_t sciInt;
        uint32_t smiCmd;
        uint8_t acpiEnableCmd;
        uint8_t acpiDisableCmd;
        uint8_t s4BiosReq;
        uint8_t pstateCount;
        uint32_t pm1aEventBlock;
        uint32_t pm1bEventBlock;
        uint32_t pm1aCtrlBlock;
        uint32_t pm1bCtrlBlock;
        uint32_t pm2CtrlBlock;
        uint32_t pmTimerBlock;
        uint32_t gpe0Block;
        uint32_t gpe1Block;
        uint8_t pm1EventLength;
        uint8_t pm1CtrlLength;
        uint8_t pm2CtrlLength;
        uint8_t pmTimerLength;
        uint8_t gpe0BlockLength;
        uint8_t gpe1BlockLength;
        uint8_t gpe1Base;
        uint8_t cstateChangedCmd;
        uint16_t c2LatencyUs;
        uint16_t c3LatencyUs;
        uint16_t flushSize;
        uint16_t flushStride;
        uint8_t dutyOffset;
        uint8_t dutyWidth;
        uint8_t dayAlarm;
        uint8_t monthAlarm;
        uint8_t rtcCentury;
        X86BootFlags x86Flags;
        uint8_t reserved1;
        FadtFlags flags;
        GenericAddr resetReg;
        uint8_t resetValue;
        ArmBootFlags armFlags;
        uint8_t errataVersion;
        uint64_t xFirmwareCtrl;
        uint64_t xDsdt;
        GenericAddr xPm1aEventBlock;
        GenericAddr xPm1bEventBlock;
        GenericAddr xPm1aCtrlBlock;
        GenericAddr xPm1bCtrlBlock;
        GenericAddr xPm2CtrlBlock;
        GenericAddr xPmTimerBlock;
        GenericAddr xGpe0Block;
        GenericAddr xGpe1Block;
        GenericAddr sleepCtrlReg;
        GenericAddr sleepStatusReg;
        uint64_t hypervisorVendorId;
    });

    enum class FacsFlag 
    {
        S4Bios = 0,
        Supports64BitWake = 1,
    };

    enum class FacsOspmFlag
    {
        Wake64Bit = 0,
    };

    using FacsFlags = sl::Flags<FacsFlag, uint32_t>;
    using FacsOspmFlags = sl::Flags<FacsOspmFlag, uint32_t>;

    //must be 64-byte aligned
    struct SL_PACKED(Facs
    {
        uint8_t signature[4]; //"FACS"
        uint32_t length;
        uint32_t hwSignature;
        uint32_t fwWakingVector;
        uint32_t globalLock;
        FacsFlags flags;
        uint64_t xFwWakingVector;
        uint8_t version;
        uint8_t reserved0[3];
        FacsOspmFlags ospmFlags;
        uint8_t reserved1[24];
    });
}
