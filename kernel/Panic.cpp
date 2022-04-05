#include <Panic.h>
#include <Locks.h>
#include <Log.h>
#include <devices/LApic.h>
#include <devices/DeviceManager.h>
#include <scheduling/Scheduler.h>
#include <StackTrace.h>

namespace Kernel
{
    struct PanicData
    {
        const char* message;
        uint8_t responseCore; //which core is handling the panic response, all others halt
        char lock;
    };
    PanicData details;

    void InitPanic()
    {
        details.message = "";
        sl::SpinlockRelease(&details.lock);
    }

    [[noreturn]]
    void Panic(const char* message)
    {
        sl::SpinlockAcquire(&details.lock);
        Scheduling::Scheduler::Global()->Suspend(true);
        details.message = message;
        details.responseCore = GetCoreLocal()->apicId;

        CPU::SetInterruptsFlag();
        asm volatile("int $0xFE");
        __builtin_unreachable();
    }

    void PanicInternal(StoredRegisters* regs)
    {
        const Scheduling::Thread* currentThread = Scheduling::Thread::Current();

        if (details.responseCore != GetCoreLocal()->apicId)
            goto final_loop_no_log;
        
        //this core has already claimed the panic response, all other cores will jump straight to halt
        Devices::LApic::Local()->BroadcastIpi(INT_VECTOR_PANIC, false);
        EnableLogDestinaton(LogDestination::FramebufferOverwrite, true);

        Logf("Thread %u (core %u) triggered kernel panic.", LogSeverity::Info, Scheduling::Thread::Current()->Id(), details.responseCore);
        Log("Reason: ", LogSeverity::Info);
        Log(details.message, LogSeverity::Info);

        Log("---- General Registers: ----", LogSeverity::Info);
        Logf("rax: 0x%0lx, rbx: 0x%0lx, rcx: 0x%0lx, rdx: 0x%0lx", LogSeverity::Info, regs->rax, regs->rbx, regs->rcx, regs->rdx);
        Logf("rsi: 0x%0lx, rdi: 0x%0lx, rsp: 0x%0lx, rbp: 0x%0lx", LogSeverity::Info, regs->rsi, regs->rdi, regs->rsp, regs->rbp);
        Logf(" r8: 0x%0lx,  r9: 0x%0lx, r10: 0x%0lx, r11: 0x%0lx", LogSeverity::Info, regs->r8, regs->r9, regs->r10, regs->r11);
        Logf("r12: 0x%0lx, r13: 0x%0lx, r14: 0x%0lx, r15: 0x%0lx", LogSeverity::Info, regs->r12, regs->r13, regs->r14, regs->r15);

        Log("---- Control Registers: ----", LogSeverity::Info);
        Logf("cr0: 0x%0lx, cr2: 0x%0lx, cr3: 0x%0lx, cr4: 0x%0lx", LogSeverity::Info, ReadCR0(), ReadCR2(), ReadCR3(), ReadCR4());
        Logf("EFER: 0x%0lx", LogSeverity::Info, CPU::ReadMsr(MSR_IA32_EFER));

        Log("---- Return Frame: ----", LogSeverity::Info);
        Logf("stack: 0x%x:0x%lx", LogSeverity::Info, regs->iret_ss, regs->iret_rsp);
        Logf("code:  0x%x:0x%lx", LogSeverity::Info, regs->iret_cs, regs->iret_rip);
        Logf("flags: 0x%lx", LogSeverity::Info, regs->iret_flags);

        if (currentThread == nullptr)
            goto final_loop;
        
        Log("---- Thread: ----", LogSeverity::Info);
        Logf("id: %u, flags:0x%lx, state:%x", LogSeverity::Info, currentThread->Id(), (size_t)currentThread->Flags(), (size_t)currentThread->State());
        if (currentThread->Parent() == nullptr)
            goto final_loop;
        Logf("parent id: %u", LogSeverity::Info, currentThread->Parent()->Id());

        Log("---- Virtual Memory Ranges: ----", LogSeverity::Info);
        currentThread->Parent()->VMM()->PrintLog();
        //TODO: would be nice to print details about current thread (id, name, sibling processes. Basic code/data locations)
        //and a stack trace of course!
    final_loop:
        Log("Panic complete. All cores halted indefinitely until manual system reset.", LogSeverity::Info);
    final_loop_no_log:
        while (1)
            CPU::Halt();
    }
}
