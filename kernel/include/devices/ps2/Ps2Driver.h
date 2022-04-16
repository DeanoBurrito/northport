#pragma once

#include <drivers/GenericDriver.h>
#include <devices/ps2/Ps2Keyboard.h>
#include <devices/ps2/Ps2Mouse.h>

namespace Kernel::Devices::Ps2
{
    using namespace Kernel::Drivers;
    
    class Ps2Driver : public GenericDriver
    {
    private:
        static bool InputBufferEmpty();
        static bool OutputBufferEmpty();
        static void WriteCmd(uint8_t cmd);
        static void WriteCmd(uint8_t cmd, uint8_t data);
        static uint8_t ReadCmd(uint8_t cmd);

    public:
        static bool Available();
        static Ps2Mouse* Mouse();
        static Ps2Keyboard* Keyboard();
        
        ~Ps2Driver() = default;

        void Init(DriverInitInfo* initInfo) override;
        void Deinit() override;
        void HandleEvent(DriverEventType type, void* eventArg) override;
    };

    GenericDriver* CreateNewPs2Driver();
}
