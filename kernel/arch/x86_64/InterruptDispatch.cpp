#include <arch/x86_64/Idt.h>
#include <Log.h>
#include <devices/LApic.h>
#include <devices/Ps2Controller.h>

/*
    This file contains a single function, however it makes calls to all sorts of classes. 
    In order to avoid polluting Idt.cpp with access to code it dosnt need, I've moved InterruptDispatch()
    here on its own.
*/
namespace Kernel
{
    [[gnu::used]]
    StoredRegisters* InterruptDispatch(StoredRegisters* regs)
    {
        switch (regs->vectorNumber)
        {
            case INTERRUPT_GSI_PS2KEYBOARD:
                Devices::Ps2Controller::Keyboard()->HandleIrq();
                break;
            
        default:
            Log("Received interrupt for unexpected vector.", LogSeverity::Error);
            break;
        }    

        Devices::LApic::Local()->SendEOI();
        //just return the existing registers
        //TODO: we should check here if regs == nullptr
        return regs;
    }
}
