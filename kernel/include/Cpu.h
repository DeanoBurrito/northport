#pragma once

#include <stdint.h>

namespace Kernel
{
    class CPU
    {
    private:

    public:
        static void DoCpuId();

        static void PortWrite8(uint16_t port, uint8_t data);
        static void PortWrite16(uint16_t port, uint16_t data);
        static void PortWrite32(uint16_t port, uint32_t data);
        static uint8_t PortRead8(uint16_t port);
        static uint16_t PortRead16(uint16_t port);
        static uint32_t PortRead32(uint16_t port);
    };
}
