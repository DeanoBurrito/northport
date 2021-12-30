#include <devices/Ps2Controller.h>
#include <acpi/AcpiTables.h>
#include <Memory.h>
#include <Platform.h>
#include <Cpu.h>
#include <Log.h>

#define PS2_WRITE_CMD(cmd) \
    while(!InputBufferEmpty()); \
    CPU::PortWrite8(PORT_PS2_COMMAND_STATUS, cmd); \

#define PS2_WRITE_CMD2(cmd, val) \
    while(!InputBufferEmpty()); \
    CPU::PortWrite8(PORT_PS2_COMMAND_STATUS, cmd); \
    while (!InputBufferEmpty()); \
    CPU::PortWrite8(PORT_PS2_DATA, val); \

#define PS2_READ_CMD(cmd, var) \
    while (!InputBufferEmpty()); \
    CPU::PortWrite8(PORT_PS2_COMMAND_STATUS, cmd); \
    while (OutputBufferEmpty()); \
    var = CPU::PortRead8(PORT_PS2_DATA);

namespace Kernel::Devices
{   
    void Ps2Device::WriteByte(uint8_t data)
    {
        if (useSecondaryPort)
        {
            while (!Ps2Controller::InputBufferEmpty());
            CPU::PortWrite8(PORT_PS2_COMMAND_STATUS, 0xD4); //send next byte to port 2
        }
        
        while (!Ps2Controller::InputBufferEmpty());
        CPU::PortWrite8(PORT_PS2_DATA, data);

    }

    uint8_t Ps2Device::ReadByte(bool ignoreOutBuffStatus)
    {
        while (!ignoreOutBuffStatus && Ps2Controller::OutputBufferEmpty());

        return CPU::PortRead8(PORT_PS2_DATA);
    }
    
    bool Ps2Controller::OutputBufferEmpty()
    {
        return (CPU::PortRead8(PORT_PS2_COMMAND_STATUS) & (1 << 0)) == 0;
    }

    bool Ps2Controller::InputBufferEmpty()
    {
        return (CPU::PortRead8(PORT_PS2_COMMAND_STATUS) & (1 << 1)) == 0;
    }

    Ps2Keyboard* ps2KeyboardInstance;
    Ps2Keyboard* Ps2Controller::Keyboard()
    { 
        if (ps2KeyboardInstance == nullptr)
        {
            ps2KeyboardInstance = new Ps2Keyboard();
            ps2KeyboardInstance->available = false;
        }
        return ps2KeyboardInstance;
    }

    Ps2Mouse* ps2MouseInstance;
    Ps2Mouse* Ps2Controller::Mouse()
    { 
        if (ps2MouseInstance == nullptr)
        {
            ps2MouseInstance = new Ps2Mouse();
            ps2MouseInstance->available = false;
        }
        return ps2MouseInstance;
    }
    
    size_t Ps2Controller::InitController()
    {
        using namespace ACPI;
        FADT* fadt = static_cast<FADT*>(AcpiTables::Global()->Find(SdtSignature::FADT));

        if (fadt == nullptr)
            Log("FADT not available, PS/2 Controller defaulting to initialize-anyway", LogSeverity::Warning);
        else if (!sl::EnumHasFlag(fadt->iaPcBootFlags, IaPcBootArchFlags::Ps2ControllerAvailable) && AcpiTables::Global()->GetRevision() > 0)
        {
            Log("ACPI says PS/2 controller is not available on this system. Aborting controller init.", LogSeverity::Info);
            return 0;
        }

        //disable both ports (controller will ignore port 2 commands if its not available, so thats safe)
        PS2_WRITE_CMD(PS2_CMD_DISABLE_KB_PORT)
        PS2_WRITE_CMD(PS2_CMD_DISABLE_MOUSE_PORT)

        //do a dummy read from the data port, make sure output buffers are clear
        if (!OutputBufferEmpty())
            CPU::PortRead8(PORT_PS2_DATA);

        //set controller to known state
        uint8_t config;
        PS2_READ_CMD(PS2_CMD_READ_CONFIG, config)
        config &= (0b1011'1100); //disable interrupts for both ports, and enable time out errors. Leave everything else as is
        PS2_WRITE_CMD2(PS2_CMD_WRITE_CONFIG, config)

        //TODO: we should test for dual channels here, but I'm yet to find any reliable documenation on how to do that.
        bool dualPortsAvail = true;
        //NOTE: this is where we'd test the controller + devices, after discovering if they're available.

        //enable both ports
        PS2_WRITE_CMD(PS2_CMD_ENABLE_KB_PORT)
        if (dualPortsAvail)
            PS2_WRITE_CMD(PS2_CMD_ENABLE_MOUSE_PORT)

        //re-enable interrupts for both ports
        PS2_READ_CMD(PS2_CMD_READ_CONFIG, config)
        config |= (1 << 0); //enable interrupts for first port
        if (dualPortsAvail)
            config |= (1 << 1); //enable port 2 interrupts if available
        PS2_WRITE_CMD2(PS2_CMD_WRITE_CONFIG, config)

        //TODO: we should optionally support quierying ps/2 devices using identify/disable-scanning commands here, so get exactly what we're using.
        Logf("PS/2 Controller initialized: dualPorts=%b", LogSeverity::Info, dualPortsAvail);
        return dualPortsAvail ? 2 : 1;
    }
}
