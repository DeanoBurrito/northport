#pragma once

#include <devices/interfaces/GenericMouse.h>

namespace Kernel::Devices::Ps2
{
    class Ps2Driver;
    
    class Ps2Mouse : public Interfaces::GenericMouse
    {
    friend Ps2Driver;
    protected:
        bool useSecondaryPort;
        size_t inputLength;
        uint8_t* inputBuffer;

        void Init() override;
        void Deinit() override;

    public:
        ~Ps2Mouse() = default;
        void Reset() override;
        sl::Opt<Drivers::GenericDriver*> GetDriverInstance() override;

        size_t ButtonCount() override;
        size_t AxisCount() override;
    };
}
