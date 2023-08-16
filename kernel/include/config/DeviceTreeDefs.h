#pragma once

#include <stdint.h>

namespace Npk::Config
{
    constexpr uint32_t FdtMagic = 0xD00DFEED;
    constexpr uint32_t FdtBeginNode = 1;
    constexpr uint32_t FdtEndNode = 2;
    constexpr uint32_t FdtProp = 3;
    constexpr uint32_t FdtNop = 4;

    constexpr uint32_t FdtCellSize = 4;

    struct FdtHeader
    {
        uint32_t magic;
        uint32_t totalSize;
        uint32_t offsetStructs;
        uint32_t offsetStrings;
        uint32_t offsetMemmapRsvd;
        uint32_t version;
        uint32_t lastCompVersion;
        uint32_t bootCpuId;
        uint32_t sizeStrings;
        uint32_t sizeStructs;
    };

    struct FdtReservedMemEntry
    {
        uint64_t base;
        uint64_t length;
    };

    struct FdtProperty
    {
        uint32_t length;
        uint32_t nameOffset;
    };
}
