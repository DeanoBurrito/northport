#pragma once

#include <stdint.h>
#include <Optional.h>

namespace Ps2
{
    bool InitController();

    bool InputReady();
    bool OutputReady();

    void SendByte(bool port2, uint8_t data);
    sl::Opt<uint8_t> ReadByte(bool wait);
    void EnableInterrupts(bool port2, bool yes);

    size_t GetKeyboardIrqNum();
    size_t GetMouseIrqNum();
}
