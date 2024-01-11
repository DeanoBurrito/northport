#pragma once

#include <stdint.h>
#include <String.h>

namespace Pci
{
    void InitNameLookup();
    sl::String PciClassToName(uint32_t id);
}
