#pragma once

#include "Types.hpp"
#include "Flags.hpp"

#ifdef __x86_64__
#define NPK_EFI_CALL __attribute__((ms_abi))
#else
#define NPK_EFI_CALL
#endif

namespace sl
{
    using EfiStatus = int;
    using EfiUintN = uintptr_t;
    using EfiChar16 = uint16_t;
    using EfiGuid = uint64_t[2];
    using EfiPhysicalAddress = uint64_t;
    using EfiVirtualAddress = uint64_t;
    using EfiHandle = void*;

    enum class EfiDaylightFlag
    {
        AdjustDaylight,
        InDaylight,
    };

    using EfiDaylightFlags = sl::Flags<EfiDaylightFlag, uint8_t>;
    
    constexpr int16_t EfiUnspecifiedTimezone = 0x07FF;

    struct EfiTime
    {
        uint16_t year; //+1900
        uint8_t month; //1 - 12
        uint8_t day; //1 - 31
        uint8_t hour; //0 - 23
        uint8_t minute; //0 - 59
        uint8_t second; //0 - 59
        uint8_t pad1;
        uint32_t nanosecond;
        int16_t timezone; //-1440 - +1440, or 2047
        EfiDaylightFlags daylight;
        uint8_t pad2;
    };

    struct EfiTimeCapabilities
    {
        uint32_t resolution;
        uint32_t accuracy;
        bool setsToZero;
    };

    enum class EfiVariableFlag
    {
        NonVolatile,
        BootAccess,
        RuntimeAccess,
        HardwareErrorRecord,
        AuthenticatedWriteAccess,
        TimeBasedAuthenticatedWriteAccess,
        AppendWrite,
        EnhancedAuthenticatedWriteAccess,
    };

    using EfiVariableFlags = sl::Flags<EfiVariableFlag, uint32_t>;

    enum class EfiMemoryType : uint32_t
    {
        Reserved,
        LoaderCode,
        LoaderData,
        BootServicesCode,
        BootServicesData,
        RuntimeServicesCode,
        RuntimeServicesData,
        Conventional,
        Unusable,
        AcpiReclaim,
        AcpiNvs,
        MappedIo,
        MappedIoPortSpace,
        PalCode,
        Persistent,
        Unaccepted,
    };

    enum class EfiMemoryFlag
    {
        Uc,
        Wc,
        Wt,
        Wb,
        Uce,

        Wp = 12,
        Rp,
        Xp,
        Nv,
        MoreReliable,
        Ro,
        Sp,
        CpuCrypto,

        IsaValid = 62,
        Runtime,
    };

    using EfiMemoryFlags = sl::Flags<EfiMemoryFlag, uint64_t>;

    constexpr uint32_t EfiMemoryDescriptorVersion = 1;

    struct EfiMemoryDescriptor
    {
        EfiMemoryType type;
        EfiPhysicalAddress physicalStart;
        EfiVirtualAddress virtualStart;
        uint64_t numberOfPages;
        EfiMemoryFlags attributes;
    };

    enum class EfiResetType
    {
        Cold,
        Warm,
        Shutdown,
        PlatformSpecific,
    };

    struct EfiCapsuleHeader
    {
        EfiGuid capsuleGuid;
        uint32_t headerSize;
        uint32_t flags;
        uint32_t capsuleImageSize;
    };

    struct EfiCapsuleBlockDescriptor
    {
        uint64_t length;
        union
        {
            EfiPhysicalAddress dataBlock;
            EfiPhysicalAddress continuationPointer;
        } _union;
    };

    struct EfiTableHeader
    {
        uint64_t signature;
        uint32_t revision;
        uint32_t headerSize;
        uint32_t crc32;
        uint32_t reserved;
    };

    constexpr uint64_t EfiSignatureSystemTable = 0x5453595320494249;
    constexpr uint64_t EfiSignatureBsTable = 0x56524553544f4f42;
    constexpr uint64_t EfiSignatureRtsTable = 0x56524553544e5552;

    using EfiGetTime = EfiStatus (NPK_EFI_CALL*)
        (EfiTime* time, EfiTimeCapabilities* caps);

    using EfiSetTime = EfiStatus (NPK_EFI_CALL*)
        (EfiTime* time);

    using EfiGetWakeupTime = EfiStatus (NPK_EFI_CALL*)
        (bool* enabled, bool* pending, EfiTime* time);

    using EfiSetWakeupTime = EfiStatus (NPK_EFI_CALL*)
        (bool enable, EfiTime* time);

    using EfiSetVirtualAddressMap = EfiStatus (NPK_EFI_CALL*)
        (EfiUintN memoryMapSize, EfiUintN descriptorSize, 
        uint32_t descriptorVersion, EfiMemoryDescriptor* virtualMap);

    using EfiConvertPointer = EfiStatus (NPK_EFI_CALL*)
        (EfiUintN debugDisposition, void** address);

    using EfiGetVariable = EfiStatus (NPK_EFI_CALL*)
        (EfiChar16* name, EfiGuid* vendor, EfiVariableFlags* attribs,
        EfiUintN* size, void* data);

    using EfiGetNextVariableName = EfiStatus (NPK_EFI_CALL*)
        (EfiUintN* size, EfiChar16* name, EfiGuid* vendor);

    using EfiSetVariable = EfiStatus (NPK_EFI_CALL*)
        (EfiChar16* name, EfiGuid* guid, EfiVariableFlags* attribs, 
        EfiUintN size, void* data);

    using EfiGetNextHighMonotonicCount = EfiStatus (NPK_EFI_CALL*)
        (uint32_t* count);

    using EfiResetSystem = void (NPK_EFI_CALL*)
        (EfiResetType type, EfiStatus status, EfiUintN size, void* data);

    using EfiUpdateCapsule = EfiStatus (NPK_EFI_CALL*)
        (EfiCapsuleHeader** capsuleHeaderArray, EfiUintN capsuleCount, 
        EfiPhysicalAddress scatterGatherList);

    using EfiQueryCapsuleCapabilities = EfiStatus (NPK_EFI_CALL*)
        (EfiCapsuleHeader** capsuleHeaderArray, EfiUintN capsuleCount,
        uint64_t maximumCapsuleSize, EfiResetType* resetType);

    using EfiQueryVariableInfo = EfiStatus (NPK_EFI_CALL*)
        (EfiVariableFlags attribs, uint64_t maximumVariableStorage, 
        uint64_t remainingVariableStorage, uint64_t* maximumVariable);

    struct EfiRuntimeServices
    {
        EfiTableHeader hdr;

        EfiGetTime GetTime;
        EfiSetTime SetTime;
        EfiGetWakeupTime GetWakeupTime;
        EfiSetWakeupTime SetWakeupTime;
        EfiSetVirtualAddressMap SetVirtualAddressMap;
        EfiConvertPointer ConvertPointer;
        EfiGetVariable GetVariable;
        EfiGetNextVariableName GetNextVariableName;
        EfiSetVariable SetVariable;
        EfiGetNextHighMonotonicCount GetNextHighMonotonicCount;
        EfiResetSystem ResetSystem;
        EfiUpdateCapsule UpdateCapsule;
        EfiQueryCapsuleCapabilities QueryCapsuleCapabilities;
        EfiQueryVariableInfo QueryVariableInfo;
    };

    struct EfiSimpleTextInputProtocol
    {};

    struct EfiSimpleTextOutputProtocol
    {};

    struct EfiBootServices
    {};

    struct EfiConfigurationTable
    {
        EfiGuid vendorGuid;
        void* vendorTable;
    };

    struct EfiSystemTable
    {
        EfiTableHeader hdr;
        EfiChar16* firmwareVendor;
        uint32_t firmwareRevision;
        EfiHandle consoleInHandle;
        EfiSimpleTextInputProtocol* conIn;
        EfiHandle consoleOutHandle;
        EfiSimpleTextOutputProtocol* conOut;
        EfiHandle standardErrorHandle;
        EfiSimpleTextOutputProtocol* stdErr;
        EfiRuntimeServices* runtimeServices;
        EfiBootServices* bootServices;
        EfiUintN numberOfTableEntries;
        EfiConfigurationTable* configTable;
    };
}
