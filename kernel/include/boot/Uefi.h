#pragma once

/*
    This file contains definitions and macros needed to interface with
    UEFI runtime services. All definitions were written based on the UEFI
    specification, v6 at the time of writing.
*/

#include <NativePtr.h>

namespace Kernel::Boot
{
    using EfiStatus = NativeUInt;

    struct [[gnu::packed]] EfiTime
    {
        uint16_t year;
        uint8_t month;
        uint8_t day;
        uint8_t hour;
        uint8_t minute;
        uint8_t second;
        uint8_t pad1;
        uint32_t nanosecond;
        int16_t timezone;
        uint8_t daylight;
        uint8_t pad2;
    };

    struct [[gnu::packed]] EfiTimeCapabilities
    {
        uint32_t resolution;
        uint32_t accuracy;
        bool setsToZero;
    };

    enum class ResetSystemType
    {
        Cold = 0,
        Warm = 1,
        Shutdown = 2,
    };

#define EFI_FUNC EfiStatus (__attribute((ms_abi)) *)

    using GetTimeType =                 EFI_FUNC(EfiTime* time, EfiTimeCapabilities* capabilities);
    using GetTimeType =                 EFI_FUNC(EfiTime* time, EfiTimeCapabilities* capabilities);
    using SetTimeType =                 EFI_FUNC(EfiTime* time);
    using GetWakeupTimeType =           EFI_FUNC(bool enabled, bool pending, EfiTime* time);
    using SetWakeupTimeType =           EFI_FUNC(bool enable, EfiTime* time);
    using SetVirtualAddressMapType =    EFI_FUNC(NativeUInt mapSize, NativeUInt descriptorSize, uint32_t descriptorVer, void* virtualMap);
    using ConvertPointerType =          EFI_FUNC(NativeUInt debugDisposition, void** address);
    using GetVariableType =             EFI_FUNC(uint16_t* name, void* guid, uint32_t attribs, NativeUInt* dataSize, void* data);
    using GetNextVariableNameType =     EFI_FUNC(NativeUInt* nameSize, uint16_t* name, void* guid);
    using SetVariableType =             EFI_FUNC(uint16_t* name, void* guid, uint32_t attribs, NativeUInt* dataSize, void* data);
    using GetNextHighMonoCountType =    EFI_FUNC(uint64_t* highCount);
    using ResetSystemFuncType =         EFI_FUNC(ResetSystemType type, EfiStatus status, NativeUInt dataSize, uint16_t* data);
    using UpdateCapsuleType = void*;
    using QueryCapsuleCapabilitiesType = void*;
    using QueryVariableInfo = void*;

#undef EFI_FUNC

    struct [[gnu::packed]] EfiTableHeader
    {
        uint64_t siganture;
        uint32_t revision;
        uint32_t headerSize;
        uint32_t crc;
        uint32_t reserved;
    };

    constexpr uint64_t EfiRuntimeServicesSignature = 0x56524553544e5552;
    
    struct [[gnu::packed]] EfiRuntimeServices : public EfiTableHeader
    {
        GetTimeType GetTime;
        SetTimeType SetTime;
        GetWakeupTimeType GetWakeupTime;
        SetWakeupTimeType SetWakeupTime;

        SetVirtualAddressMapType SetVirtualAddressMap;
        ConvertPointerType ConvertPointer;

        GetVariableType GetVariable;
        GetNextVariableNameType GetNextVariableName;
        SetVariableType SetVariable;

        GetNextHighMonoCountType GetNextHighMonotonicCount;
        ResetSystemFuncType ResetSystem;
        UpdateCapsuleType UpdateCapsule;
        QueryCapsuleCapabilitiesType QueryCapsuleCapabilities;
        QueryVariableInfo QueryVariableInfo;
    };
}
