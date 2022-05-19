#pragma once

#include <stdint.h>

namespace Kernel::Devices::Pci
{
    constexpr uint8_t CapIdReserved = 0x0;
    constexpr uint8_t CapIdPowerManagement = 0x1;
    constexpr uint8_t CapIdAgp = 0x2;
    constexpr uint8_t CapidVpd = 0x3;
    constexpr uint8_t CapIdSlotId = 0x4;
    constexpr uint8_t CapIdMsi = 0x5;
    constexpr uint8_t CapIdCompactHotSwap = 0x6;
    constexpr uint8_t CapIdPciX = 0x7;
    constexpr uint8_t CapIdHyperTransport = 0x8;
    constexpr uint8_t CapIdVendor = 0x9;
    constexpr uint8_t CapIdDebugPort = 0xA;
    constexpr uint8_t CapIdCompactResCtrl = 0xB;
    constexpr uint8_t CapIdHotPlug = 0xC;
    constexpr uint8_t CapIdBridgeVendorId = 0xD;
    constexpr uint8_t CapIdAgp8x = 0xE;
    constexpr uint8_t CapIdSecureDevice = 0xF;
    constexpr uint8_t CapIdPciExpress = 0x10;
    constexpr uint8_t CapIdMsiX = 0x11;
}
