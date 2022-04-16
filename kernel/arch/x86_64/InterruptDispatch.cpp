#include <arch/x86_64/Idt.h>
#include <Log.h>
#include <Panic.h>
#include <syscalls/Dispatch.h>
#include <devices/LApic.h>
#include <devices/ps2/Ps2Driver.h>
#include <devices/8254Pit.h>
#include <devices/SystemClock.h>
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
            SetPanicOverrideRegs(regs);
            Log("General Protection fault.", LogSeverity::Fatal);
            while (1)
                asm("hlt");
            return true;

        case NativeExceptions::DoubleFault:
            SetPanicOverrideRegs(regs);
            Log("Double fault.", LogSeverity::Fatal);
            while (1)
                asm("hlt");
            return true;
            
        case NativeExceptions::PageFault:
            SetPanicOverrideRegs(regs);
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

        switch ((uint8_t)regs->vectorNumber)
        {
            case INT_VECTOR_SPURIOUS:
                return returnRegs; //no need to do EOI here, just return

            case INT_VECTOR_PANIC:
                PanicInternal(regs);
                __builtin_unreachable();

            case INT_VECTOR_PS2KEYBOARD:
                if (Devices::Ps2::Ps2Driver::Keyboard() != nullptr)
                    Devices::Ps2::Ps2Driver::Keyboard()->HandleIrq();
                break;

            case INT_VECTOR_PS2MOUSE:
                if (Devices::Ps2::Ps2Driver::Mouse() != nullptr)
                    Devices::Ps2::Ps2Driver::Mouse()->HandleIrq();
                break;

            case INT_VECTOR_SCHEDULER_TICK:
                if (Devices::UsingApicForUptime() && Devices::LApic::Local()->IsBsp())
                    Devices::IncrementUptime(Devices::LApic::Local()->GetTimerIntervalMS());
                returnRegs = Scheduling::Scheduler::Global()->Tick(regs);
                break;

            case INT_VECTOR_PIT_TICK:
                if (!Devices::UsingApicForUptime())
                    Devices::IncrementUptime(1); //we hardcode the PIT to ~1ms ticks
                Devices::PitHandleIrq();
                break;

            case INT_VECTOR_SYSCALL:
                returnRegs = Syscalls::EnterSyscall(regs);
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
