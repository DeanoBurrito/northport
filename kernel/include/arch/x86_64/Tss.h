#pragma once

#include <stdint.h>

namespace Kernel
{
    struct [[gnu::packed]] TaskStateSegment
    {
        uint32_t reserved0;
        uint64_t rsp0;
        uint64_t rsp1;
        uint64_t rsp2;
        uint64_t reserved1;
        uint64_t ist1;
        uint64_t ist2;
        uint64_t ist3;
        uint64_t ist4;
        uint64_t ist5;
        uint64_t ist6;
        uint64_t ist7;
        uint64_t reserved2;
        uint16_t reserved3;
        uint16_t ioBitmapOffset;
    };

    void FlushTSS();
    TaskStateSegment* CurrentTss();
}
