#pragma once

#include <stdint.h>

namespace Npk
{
    enum class BootInfoType : uint16_t
    {
        Last = 0,
        MachType = 1,
        CpuType = 2,
        FpuType = 3,
        MmuType = 4,
        MemChunk = 5,
        RamDisk = 6,
        CommandLine = 7,
        RngSeed = 8,

        QemuVersion = 0x8000,
        GoldfishPicBase = 0x8001,
        GoldfishRtcBase = 0x8002,
        GoldfishTtyBase = 0x8003,
        VirtioBase = 0x8004,
        ControlBase = 0x8005
    };

    struct [[gnu::packed]] BootInfoTag
    {
        BootInfoType type;
        uint16_t size;
    };

    struct [[gnu::packed]] BootInfoMemChunk
    {
        uint32_t addr;
        uint32_t size;
    };
}
