#include <devices/ps2/Ps2Driver.h>
#include <devices/ps2/Ps2Keyboard.h>
#include <devices/ps2/Ps2Mouse.h>
#include <devices/DeviceManager.h>
#include <acpi/AcpiTables.h>
#include <Platform.h>
#include <Cpu.h>
#include <Log.h>

namespace Kernel::Devices::Ps2
{
    bool InputBufferEmpty()
    { return (CPU::PortRead8(PORT_PS2_COMMAND_STATUS) & (1 << 1)) == 0; }

    bool OutputBufferEmpty()
    { return (CPU::PortRead8(PORT_PS2_COMMAND_STATUS) & (1 << 0)) == 0; }
    
    void WriteCmd(uint8_t cmd)
    {
        while (!InputBufferEmpty());
        CPU::PortWrite8(PORT_PS2_COMMAND_STATUS, cmd);
    }

    void WriteCmd(uint8_t cmd, uint8_t data)
    {
        while (!InputBufferEmpty());
        CPU::PortWrite8(PORT_PS2_COMMAND_STATUS, cmd);

        while (!InputBufferEmpty());
        CPU::PortWrite8(PORT_PS2_DATA, data);
    }

    uint8_t ReadCmd(uint8_t cmd)
    {
        while (!InputBufferEmpty());
        CPU::PortWrite8(PORT_PS2_COMMAND_STATUS, cmd);

        while (OutputBufferEmpty());
        return CPU::PortRead8(PORT_PS2_DATA);
    }

    void WriteData(bool secondary, uint8_t data)
    {
        if (secondary)
            CPU::PortWrite8(PORT_PS2_COMMAND_STATUS, Cmds::NextDataToPortB);
        
        while (!InputBufferEmpty());
        CPU::PortWrite8(PORT_PS2_DATA, data);
    }
    
    uint8_t ReadData()
    {
        while (OutputBufferEmpty());
        return CPU::PortRead8(PORT_PS2_DATA);
    }
    
    GenericDriver* CreateNewPs2Driver()
    { return new Ps2Driver(); }

    bool Ps2Driver::Available()
    {
#ifndef __x86_64__
        //we only support ps2 x86 platforms (in this case we dont support < 64bit cpus)
        return false;
#endif

        //later ACPI revisions will hint that the ps2 peripherals no longer exist, check for that
        using namespace ACPI;
        FADT* fadt = static_cast<FADT*>(AcpiTables::Global()->Find(SdtSignature::FADT));
        if (fadt == nullptr)
            Log("FADT not available, PS/2 driver will try to initialize anywhere.", LogSeverity::Warning);
        else if (!sl::EnumHasFlag(fadt->iaPcBootFlags, IaPcBootArchFlags::Ps2ControllerAvailable) 
            && AcpiTables::Global()->GetRevision() > 0)
        {
            Log("ACPI says PS/2 peripherals are not available on this platform, will not initialize driver.", LogSeverity::Info);
            return false;
        }

        return true;
    }

    Ps2Driver* ps2DriverInstance = nullptr;
    Ps2Mouse* mouse = nullptr;
    Ps2Keyboard* keyboard = nullptr;

    Ps2Mouse* Ps2Driver::Mouse()
    { return mouse; }

    Ps2Keyboard* Ps2Driver::Keyboard()
    { return keyboard; }
    
    void Ps2Driver::Init(DriverInitInfo*)
    {
        if (ps2DriverInstance != nullptr)
            ps2DriverInstance = this;
        
        //start by setting up the ps2 controller: disabling both ports so we dont get interactions while configuring the controller
        WriteCmd(Cmds::DisableKeyboardPort);
        WriteCmd(Cmds::DisableMousePort);
        
        while (!OutputBufferEmpty())
            CPU::PortRead8(PORT_PS2_DATA);
        
        uint8_t config = ReadCmd(Cmds::ReadConfig);
        config &= 0b1011'1100; //preserve reserved bits, disable interrupts for both ports/
        WriteCmd(Cmds::WriteConfig, config);

        //TODO: check which ports have devices attached, and what they are (we should support all configs).
        bool keyboardSupported = true;
        bool mouseSupported = true;

        WriteCmd(Cmds::EnableKeyboardPort);
        WriteCmd(Cmds::EnableMousePort);

        config = ReadCmd(Cmds::ReadConfig);
        config |= 0b11; //bits for ports 1 & 2
        WriteCmd(Cmds::WriteConfig, config);

        Log("PS/2 controller initialized, registering devices on active ports.", LogSeverity::Info);

        if (keyboardSupported)
        {
            keyboard = new Ps2Keyboard();
            keyboard->useSecondaryPort = false;
            DeviceManager::Global()->RegisterDevice(keyboard);
            DeviceManager::Global()->SetPrimaryDevice(DeviceType::Keyboard, keyboard->GetId());
        }
        else
            keyboard = nullptr;

        if (mouseSupported)
        {
            mouse = new Ps2Mouse();
            mouse->useSecondaryPort = true;
            DeviceManager::Global()->RegisterDevice(mouse);
            DeviceManager::Global()->SetPrimaryDevice(DeviceType::Mouse, mouse->GetId());
        }
        else
            mouse = nullptr;
    }

    void Ps2Driver::Deinit()
    {
        //disable ps2 ports
        WriteCmd(Cmds::DisableKeyboardPort);
        WriteCmd(Cmds::DisableMousePort);
        
        if (keyboard != nullptr)
        {
            delete DeviceManager::Global()->UnregisterDevice(keyboard->GetId());
            keyboard = nullptr;
        }

        if (mouse != nullptr)
        {
            delete DeviceManager::Global()->UnregisterDevice(mouse->GetId());
            mouse = nullptr;
        }

        ps2DriverInstance = nullptr;
    }

    void Ps2Driver::HandleEvent(DriverEventType, void*)
    { }

    void Ps2Driver::ResetSystem()
    {
        WriteCmd(Cmds::TheBigReset);
    }
}
