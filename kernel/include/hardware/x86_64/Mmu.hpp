#pragma once

#include <hardware/Arch.hpp>

namespace Npk
{
    constexpr size_t PtEntries = 512;

    struct PageTable
    {
        uint64_t ptes[PtEntries];
    };

    extern PageTable* kernelMap;
    extern Paddr apBootPage;
}
