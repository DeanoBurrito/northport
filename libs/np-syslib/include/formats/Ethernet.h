#pragma once

#include <Types.h>

namespace sl
{
    constexpr unsigned EthernetFrameSize = 1522;
    constexpr unsigned MacAddrBytes = 6;

    struct MacAddr
    {
        uint8_t bytes[MacAddrBytes];

        inline uint32_t Low32()
        {
            uint32_t data = bytes[0];
            data |= ((uint32_t)bytes[1] << 8);
            data |= ((uint32_t)bytes[2] << 16);
            data |= ((uint32_t)bytes[3] << 24);
            return data;
        }

        inline uint16_t High16()
        {
            return bytes[4] | ((uint16_t)bytes[5] << 8);
        }
    };
}
