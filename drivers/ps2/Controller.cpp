#include <Controller.h>
#include <interfaces/driver/Api.h>
#include <Log.h>
#include <ArchHints.h>
#include <Locks.h>

namespace Ps2
{
    constexpr uint8_t CmdDisablePort1 = 0xAD;
    constexpr uint8_t CmdEnablePort1 = 0xAE;
    constexpr uint8_t CmdDisablePort2 = 0xA7;
    constexpr uint8_t CmdEnablePort2 = 0xA8;
    constexpr uint8_t CmdReadConfig = 0x20;
    constexpr uint8_t CmdWriteConfig = 0x60;
    constexpr uint8_t CmdTestController = 0xAA;
    constexpr uint8_t CmdReadOutputPort = 0xD0;
    constexpr uint8_t CmdWriteOutputPort = 0xD1;
    constexpr uint8_t CmdSendDataPort2 = 0xD4;
    constexpr uint8_t CmdResetDevice = 0xFF;

    bool dualPort;
    sl::SpinLock controllerLock;

    static void SendCommand(uint8_t byte)
    {
        while (!InputReady())
            sl::HintSpinloop();

        uintptr_t data = byte;
        VALIDATE_(npk_access_bus(npk_bus_type_port_io, 1, 0x64, &data, true), );
    }

    bool InitController()
    {
        //controller init sequence taken from: https://osdev.wiki/wiki/I8042_PS/2_Controller
        SendCommand(CmdDisablePort1);
        SendCommand(CmdDisablePort2);
        while (ReadByte(false).HasValue()) //drain any leftover data from output buffer
            sl::HintSpinloop();

        SendCommand(CmdReadConfig);
        uint8_t config = *ReadByte(true);
        config &= ~0b1000011; //disable interrupts (both ports), disable translation
        SendCommand(CmdWriteConfig);
        SendByte(false, config);

        SendCommand(CmdTestController);
        if (*ReadByte(true) != 0x55)
        {
            Log("Controller failed self test.", LogLevel::Error);
            return false;
        }

        SendCommand(CmdDisablePort1);
        SendCommand(CmdDisablePort2);
        while (ReadByte(false).HasValue())
            sl::HintSpinloop();
        SendCommand(CmdWriteConfig);
        SendByte(false, config);

        SendCommand(CmdEnablePort2);
        SendCommand(CmdReadConfig);
        if (*ReadByte(true) & (1 << 5))
            dualPort = false;
        else
            dualPort = true;
        SendCommand(CmdDisablePort2);
        Log("Controller initialized, ports=%u", LogLevel::Info, dualPort ? 2 : 1);
        
        //TODO: reset device on each port, check it responds and then determine its type.
        SendCommand(CmdEnablePort1);
        return true;
    }

    bool InputReady()
    {
        uintptr_t byte;
        VALIDATE_(npk_access_bus(npk_bus_type_port_io, 1, 0x64, &byte, false), false);

        return (byte & 0b10) == 0;
    }

    bool OutputReady()
    {
        uintptr_t byte;
        VALIDATE_(npk_access_bus(npk_bus_type_port_io, 1, 0x64, &byte, false), false);

        return (byte & 0b01) == 1;
    }

    void SendByte(bool port2, uint8_t data)
    {
        if (port2 && !dualPort)
            return;

        sl::ScopedLock scopeLock(controllerLock);
        if (port2)
            SendCommand(CmdSendDataPort2);

        while (!InputReady())
            sl::HintSpinloop();
        uintptr_t byte = data;
        VALIDATE_(npk_access_bus(npk_bus_type_port_io, 1, 0x60, &byte, true), );
    }

    sl::Opt<uint8_t> ReadByte(bool wait)
    {
        if (!wait && !OutputReady())
            return {};

        sl::ScopedLock scopeLock(controllerLock);
        while (!OutputReady())
            sl::HintSpinloop();

        uintptr_t data;
        VALIDATE_(npk_access_bus(npk_bus_type_port_io, 1, 0x60, &data, false), {});
        return static_cast<uint8_t>(data);
    }

    void EnableInterrupts(bool port2, bool yes)
    {
        const uint8_t mask = 1 << (port2 ? 1 : 0);

        SendCommand(CmdReadConfig);
        uint8_t config = *ReadByte(true);
        if (yes)
            config |= mask;
        else
            config &= ~mask;

        SendCommand(CmdWriteConfig);
        SendByte(false, config);
        Log("Interrupts enabled", LogLevel::Debug);
    }

    size_t GetKeyboardIrqNum()
    { return 1; }

    size_t GetMouseIrqNum()
    { return 12; }
}
