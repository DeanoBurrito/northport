#include <devices/ps2/Ps2Mouse.h>
#include <devices/ps2/Ps2Driver.h>
#include <devices/IoApic.h>
#include <devices/Keyboard.h>
#include <devices/Mouse.h>
#include <Platform.h>
#include <arch/Cpu.h>

#include <Log.h>

namespace Kernel::Devices::Ps2
{
    void Ps2Mouse::Init()
    {
        state = DeviceState::Initializing;

        inputBuffer = new uint8_t[4];
        inputLength = 0;

        leftMousePrevDown = rightMousePrevDown = middleMousePrevDown = false;

        //enable data reporting
        WriteData(useSecondaryPort, Cmds::EnableDataReporting);
        if (ReadData() != Cmds::Acknowledge)
            Log("Mouse did not acknowledge command: enable data reporting.", LogSeverity::Error);

        //remap irq12 to our interrupt vector of choice
        auto overrideDetails = IoApic::TranslateToGsi(12);
        IoApic::Global(overrideDetails.gsiNum)->WriteRedirect(0, overrideDetails);
        IoApic::Global(overrideDetails.gsiNum)->SetPinMask(overrideDetails.irqNum, false);

        state = DeviceState::Ready;
    }

    void Ps2Mouse::Deinit()
    {
        state = DeviceState::Deinitializing;

        WriteData(useSecondaryPort, Cmds::DisableDataReporting);

        state = DeviceState::Shutdown;
    }

    void Ps2Mouse::Reset()
    {
        Deinit();
        Init();
    }

    extern Ps2Driver* ps2DriverInstance;
    sl::Opt<Drivers::GenericDriver*> Ps2Mouse::GetDriverInstance()
    {
        if (ps2DriverInstance == nullptr)
            return {};
        return ps2DriverInstance;
    }

    size_t Ps2Mouse::ButtonCount()
    { return 0; }

    size_t Ps2Mouse::AxisCount()
    { return 0; }

    KeyEvent CreateKeyEvent(KeyIdentity id, bool pressed)
    {
        KeyEvent ev;
        ev.id = id;
        ev.inputDeviceId = (uint8_t)BuiltInInputDevices::Ps2Mouse;
        ev.mods = KeyModFlags::None;
        ev.tags = pressed ? KeyTags::Pressed : KeyTags::Released;
        return ev;
    }

    void Ps2Mouse::HandleIrq()
    {   
        while (!OutputBufferEmpty())
        {
            inputBuffer[inputLength] = ReadData();
            inputLength++;
        }

        if (inputLength < 3)
            return; //not a full packet
        
        int32_t relativeX = inputBuffer[1];
        if (inputBuffer[0] & (1 << 4))
            relativeX |= 0xFFFFFF00;
        
        int32_t relativeY = inputBuffer[2];
        if (inputBuffer[0] & (1 << 5))
            relativeY |= 0xFFFFFF00;

        Mouse::Global()->PushMouseEvent({ relativeX, relativeY });

        if ((inputBuffer[0] & 0b001) != 0 && !leftMousePrevDown)
            Keyboard::Global()->PushKeyEvent(CreateKeyEvent(KeyIdentity::MouseLeft, true));
        if ((inputBuffer[0] & 0b001) == 0 && leftMousePrevDown)
            Keyboard::Global()->PushKeyEvent(CreateKeyEvent(KeyIdentity::MouseLeft, true));
        leftMousePrevDown = (inputBuffer[0] & 0b001) != 0;

        if ((inputBuffer[0] & 0b010) != 0 && !rightMousePrevDown)
            Keyboard::Global()->PushKeyEvent(CreateKeyEvent(KeyIdentity::MouseRight, true));
        if ((inputBuffer[0] & 0b010) == 0 && rightMousePrevDown)
            Keyboard::Global()->PushKeyEvent(CreateKeyEvent(KeyIdentity::MouseRight, true));
        rightMousePrevDown = (inputBuffer[0] & 0b010) != 0;

        if ((inputBuffer[0] & 0b100) != 0 && !middleMousePrevDown)
            Keyboard::Global()->PushKeyEvent(CreateKeyEvent(KeyIdentity::MouseMiddle, true));
        if ((inputBuffer[0] & 0b100) == 0 && middleMousePrevDown)
            Keyboard::Global()->PushKeyEvent(CreateKeyEvent(KeyIdentity::MouseMiddle, true));
        middleMousePrevDown = (inputBuffer[0] & 0b100) != 0;
        
        inputLength = 0;
    }
}
