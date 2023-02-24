#pragma once

#include <devices/GenericDevices.h>

namespace Npk::Debug
{
    void InitEarlyTerminal();

    class TerminalSerialDriver : public Devices::GenericSerial
    {
    public:
        bool Init() override;
        bool Deinit() override;
        
        void Write(sl::Span<uint8_t> buffer) override;
        size_t Read(sl::Span<uint8_t> buffer) override;
        bool InputAvailable() override;
    };
}
