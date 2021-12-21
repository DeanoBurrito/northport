#include <arch/x86_64/Idt.h>
#include <Log.h>
#include <devices/LApic.h>
#include <devices/Ps2Controller.h>
#include <devices/8254Pit.h>
#include <scheduling/Scheduler.h>

/*
    This file contains InterruptDispatch() and friends. In order to avoid polluting Idt.cpp with access to code it dosnt need, 
    I've moved InterruptDispatch() here on its own.
*/
namespace Kernel
{
    bool TryHandleNativeException(StoredRegisters* regs)
    {
        switch (regs->vectorNumber)
        {
        case NativeExceptions::GeneralProtectionFault:
            Log("General Protection fault.", LogSeverity::Fatal);
            while (1)
                asm("hlt");
            return true;

        case NativeExceptions::DoubleFault:
            Log("Double fault.", LogSeverity::Fatal);
            while (1)
                asm("hlt");
            return true;
            
        case NativeExceptions::PageFault:
            Log("Page fault.", LogSeverity::Fatal);
            while (1)
                asm("hlt");
            return true;

        default:
            return false;
        }
    }
    
    [[gnu::used]]
    StoredRegisters* InterruptDispatch(StoredRegisters* regs)
    {
        StoredRegisters* returnRegs = regs;

        switch (regs->vectorNumber)
        {
            case INTERRUPT_GSI_SPURIOUS:
                return returnRegs; //no need to do EOI here, just return
            case INTERRUPT_GSI_IGNORE:
                Log("Received interrupt vector IGNORE, this should never actually happen.", LogSeverity::Error);
                return returnRegs; //should never occur, useful for apic timer calibration

            case INTERRUPT_GSI_PS2KEYBOARD:
                Devices::Ps2Controller::Keyboard()->HandleIrq();
                break;
            case INTERRUPT_GSI_SCHEDULER_NEXT:
                returnRegs = Scheduling::Scheduler::Local()->SelectNextThread(regs);
                break;
            case INTERRUPT_GSI_PIT_TICK:
                Devices::PitHandleIrq();
                break;
            
        default:
            if (!TryHandleNativeException(regs))
                Log("Received interrupt for unexpected vector, ignoring.", LogSeverity::Error);
            break;
        }    

        Devices::LApic::Local()->SendEOI();
        return returnRegs;
    }
}
