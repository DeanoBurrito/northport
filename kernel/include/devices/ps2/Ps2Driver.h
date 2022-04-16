#pragma once

#include <drivers/GenericDriver.h>
#include <devices/ps2/Ps2Keyboard.h>
#include <devices/ps2/Ps2Mouse.h>

namespace Kernel::Devices::Ps2
{
    using namespace Kernel::Drivers;

    namespace Cmds
    {
        //controller
        constexpr uint8_t ReadConfig = 0x20;
        constexpr uint8_t WriteConfig = 0x60;
        constexpr uint8_t EnableKeyboardPort = 0xAE;
        constexpr uint8_t DisableKeyboardPort = 0xAD;
        constexpr uint8_t EnableMousePort = 0xA8;
        constexpr uint8_t DisableMousePort = 0xA7;
        constexpr uint8_t NextDataToPortB = 0xD4;
        constexpr uint8_t TheBigReset = 0xFE;

        //mouse
        constexpr uint8_t EnableDataReporting = 0xF4;
        constexpr uint8_t DisableDataReporting = 0xF5;
        constexpr uint8_t Acknowledge = 0xFA;

        //keyboard
    };

    bool InputBufferEmpty();
    bool OutputBufferEmpty();

    void WriteCmd(uint8_t cmd);
    void WriteCmd(uint8_t cmd, uint8_t data);
    uint8_t ReadCmd(uint8_t cmd);

    void WriteData(bool secondary, uint8_t data);
    uint8_t ReadData();
    
    class Ps2Driver : public GenericDriver
    {
    public:
        static bool Available();
        static Ps2Mouse* Mouse();
        static Ps2Keyboard* Keyboard();
        
        ~Ps2Driver() = default;

        void Init(DriverInitInfo* initInfo) override;
        void Deinit() override;
        void HandleEvent(DriverEventType type, void* eventArg) override;

        //yes, this will reset the cpu.
        void ResetSystem();
    };

    GenericDriver* CreateNewPs2Driver();
}
