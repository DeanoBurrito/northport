#pragma once

#include "Types.hpp"
#include "Flags.hpp"

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

    enum class EfiMemoryType
    {
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

    using EfiGetTime = EfiStatus (*)(EfiTime* time, EfiTimeCapabilities* caps);
    using EfiSetTime = EfiStatus (*)(EfiTime* time);
    using EfiGetWakeupTime = EfiStatus (*)(bool* enabled, bool* pending, 
        EfiTime* time);
    using EfiSetWakeupTime = EfiStatus (*)(bool enable, EfiTime* time);
    using EfiSetVirtualAddressMap = EfiStatus (*)(EfiUintN memoryMapSize, 
        EfiUintN descriptorSize, uint32_t descriptorVersion, 
        EfiMemoryDescriptor* virtualMap);
    using EfiConvertPointer = EfiStatus (*)(EfiUintN debugDisposition, 
        void** address);
    using EfiGetVariable = EfiStatus (*)(EfiChar16* name, EfiGuid* vendor, 
        EfiVariableFlags* attribs, EfiUintN* size, void* data);
    using EfiGetNextVariableName = EfiStatus (*)(EfiUintN* size, 
        EfiChar16* name, EfiGuid* vendor);
    using EfiSetVariable = EfiStatus (*)(EfiChar16* name, EfiGuid* guid, 
        EfiVariableFlags* attribs, EfiUintN size, void* data);
    using EfiGetNextHighMonotonicCount = EfiStatus (*)(uint32_t* count);
    using EfiResetSystem = void (*)(EfiResetType type, EfiStatus status, 
        EfiUintN size, void* data);
    using EfiUpdateCapsule = EfiStatus (*)(EfiCapsuleHeader** capsuleHeaderArray, 
        EfiUintN capsuleCount, EfiPhysicalAddress scatterGatherList);
    using EfiQueryCapsuleCapabilities = EfiStatus (*)(
        EfiCapsuleHeader** capsuleHeaderArray, EfiUintN capsuleCount, 
        uint64_t maximumCapsuleSize, EfiResetType* resetType);
    using EfiQueryVariableInfo = EfiStatus (*)(EfiVariableFlags attribs, 
        uint64_t maximumVariableStorage, uint64_t remainingVariableStorage,
        uint64_t* maximumVariable);

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
